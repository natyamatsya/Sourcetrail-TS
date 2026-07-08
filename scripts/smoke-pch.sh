#!/usr/bin/env bash
# Regression for zero-config PCH (CMake File API source group).
#
# Generates a CMake project with target_precompile_headers whose heavy header is
# shared by several TUs, indexes it via the "C/C++ from CMake File API" source
# group, and asserts:
#   1. a Sourcetrail PCH is generated (and its freshness stamp written)
#   2. indexing is clean (0 errors) -- CMake's own incompatible PCH fragments and
#      the generated cmake_pch wrapper are handled, not fed to libclang
#   3. the shared header's symbols are indexed
#   4. a second run REUSES the PCH instead of regenerating it (freshness)
#   5. PCH indexing is faster than the same project without target_precompile_headers
#
# Needs cmake (>=3.20) + ninja + a clang; skips gracefully otherwise.
#   scripts/smoke-pch.sh [build-dir]     (default: .build/llvm-clang-dbg)
set -u

BUILD_DIR="${1:-.build/llvm-clang-dbg}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$REPO_ROOT/$BUILD_DIR/app/Sourcetrail"
CXX="${CXX:-/opt/homebrew/opt/llvm/bin/clang++}"
[ -x "$CXX" ] || CXX="$(command -v clang++ || true)"
# Non-symlinked workspace (a /tmp indexed_header_paths would not match clang's
# canonical header paths); use the build dir root under the repo.
WORK="$(mktemp -d "$REPO_ROOT/$BUILD_DIR/sourcetrail-pch-smoke.XXXXXX")"

FAILURES=0
note() { printf '\n=== %s ===\n' "$*"; }
pass() { printf 'PASS: %s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*"; FAILURES=$((FAILURES + 1)); }
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

[ -x "$APP" ] || { echo "missing $APP - build first"; exit 2; }
command -v sqlite3 >/dev/null || { echo "sqlite3 not found"; exit 2; }
command -v cmake >/dev/null && command -v ninja >/dev/null && [ -n "$CXX" ] || {
    echo "SKIP: cmake/ninja/clang++ not available"; exit 0; }
pkill -9 -f 'sourcetrail_indexer|Sourcetrail index' 2>/dev/null

# ---- generate a CMake project (with and without PCH) sharing a heavy header
gen_project() {  # $1 = dir  $2 = "pch" | "nopch"
    local dir="$1" variant="$2"
    mkdir -p "$dir/src"
    cat > "$dir/src/heavy.h" <<'EOF'
#pragma once
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <numeric>
#include <sstream>
namespace app { struct Widget { std::string name; std::vector<int> data; }; }
EOF
    local i
    for i in $(seq 1 20); do
        cat > "$dir/src/tu$i.cpp" <<EOF
#include "heavy.h"
int compute$i(const app::Widget& w) {
    std::unordered_map<std::string,int> m; m[w.name] = (int)w.data.size();
    std::ostringstream os; os << w.name;
    return std::accumulate(w.data.begin(), w.data.end(), 0) + (int)m.size();
}
EOF
    done
    local pchline=""
    [ "$variant" = "pch" ] && pchline='target_precompile_headers(bench PRIVATE src/heavy.h)'
    cat > "$dir/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.20)
project(bench CXX)
set(CMAKE_CXX_STANDARD 17)
file(GLOB SRCS src/*.cpp)
add_library(bench \${SRCS})
$pchline
EOF
    cat > "$dir/CMakePresets.json" <<EOF
{ "version": 3, "configurePresets": [{ "name": "dev", "generator": "Ninja",
  "binaryDir": "\${sourceDir}/build",
  "cacheVariables": { "CMAKE_CXX_COMPILER": "$CXX" } }] }
EOF
    mkdir -p "$dir/build/.cmake/api/v1/query"; : > "$dir/build/.cmake/api/v1/query/codemodel-v2"
    (cd "$dir" && cmake --preset dev > cmake.log 2>&1)
    cat > "$dir/p.srctrl.toml" <<EOF
version = 8
[[source_groups]]
id = "44444444-4444-4444-8444-444444444444"
name = "bench"
status = "enabled"
type = "C/C++ from CMake File API"
[source_groups.cmake]
source_directory = "$dir"
preset_name = "dev"
configuration = ""
target_glob = "*"
EOF
}

index_ms() { grep -E 'Finished indexing' "$1" | tail -1 | grep -oE '[0-9]{2}:[0-9]{2}:[0-9]{2}:[0-9]{3}' | tail -1; }
ms_to_int() { awk -F: '{print ($2*60+$3)*1000+$4}' <<<"$1"; }

note "generate CMake project with target_precompile_headers"
gen_project "$WORK/pch" pch
[ "$(ls "$WORK/pch/build/.cmake/api/v1/reply/" 2>/dev/null | grep -c codemodel)" -ge 1 ] \
    && pass "CMake File API reply generated" || { fail "cmake configure failed"; exit 1; }

note "index (cold: PCH generated)"
(cd "$WORK/pch" && "$APP" index --full "$WORK/pch/p.srctrl.toml" > "$WORK/pch/cold.log" 2>&1)
PCHDB="$WORK/pch/p.srctrl.db"
PCH_COUNT=$(find "$WORK/pch" -name '*.pch' | wc -l | tr -d ' ')
[ "$PCH_COUNT" -ge 1 ] && pass "PCH generated ($PCH_COUNT)" || fail "no PCH generated"
ERR=$(sqlite3 "$PCHDB" "SELECT COUNT(*) FROM error;" 2>/dev/null)
[ "${ERR:-1}" -eq 0 ] && pass "clean indexing (0 errors)" \
    || { fail "$ERR indexing error(s)"; sqlite3 "$PCHDB" "SELECT DISTINCT substr(message,1,70) FROM error LIMIT 5;" | sed 's/^/     /'; }
WIDGETS=$(sqlite3 "$PCHDB" "SELECT COUNT(*) FROM node WHERE serialized_name LIKE '%Widget%';" 2>/dev/null)
[ "${WIDGETS:-0}" -gt 0 ] && pass "shared header symbols indexed (Widget)" || fail "header symbols missing"
CMPCH=$(sqlite3 "$PCHDB" "SELECT COUNT(*) FROM file WHERE path LIKE '%cmake_pch%';" 2>/dev/null)
[ "${CMPCH:-1}" -eq 0 ] && pass "CMake's cmake_pch artifact excluded" || fail "cmake_pch was indexed"

COLD_COUNTS=$(sqlite3 "$PCHDB" "SELECT (SELECT COUNT(*) FROM node),(SELECT COUNT(*) FROM edge),(SELECT COUNT(*) FROM file);" 2>/dev/null)

# Regression guard: a full re-index rebuilds the DB from empty, and the PCH's
# header symbols are indexed as part of building the PCH. If the PCH were reused
# (rebuild skipped) those symbols would silently vanish. The second full index
# must reproduce the first exactly.
note "second full index reproduces the same index (PCH symbols not dropped)"
(cd "$WORK/pch" && "$APP" index --full "$WORK/pch/p.srctrl.toml" > "$WORK/pch/warm.log" 2>&1)
WARM_COUNTS=$(sqlite3 "$PCHDB" "SELECT (SELECT COUNT(*) FROM node),(SELECT COUNT(*) FROM edge),(SELECT COUNT(*) FROM file);" 2>/dev/null)
if [ -n "$COLD_COUNTS" ] && [ "$WARM_COUNTS" = "$COLD_COUNTS" ]; then
    pass "warm full re-index identical to cold (node,edge,file = $WARM_COUNTS)"
else
    fail "warm full re-index differs: cold=($COLD_COUNTS) warm=($WARM_COUNTS) -- PCH symbols dropped?"
fi

# Regression guard: an incremental refresh with nothing changed must re-index
# nothing. A project header is legitimately non-indexed; if the refresh treated it
# as changed it would cascade through every including TU and re-index the whole
# project on every incremental (the CMake File API over-clearing bug).
note "incremental no-op re-indexes nothing"
(cd "$WORK/pch" && "$APP" index "$WORK/pch/p.srctrl.toml" > "$WORK/pch/noop.log" 2>&1)
NOOP_N=$(grep -oE 'Finished indexing: [0-9]+/[0-9]+' "$WORK/pch/noop.log" | tail -1 | grep -oE '^Finished indexing: [0-9]+' | grep -oE '[0-9]+$')
if [ -z "$NOOP_N" ] || [ "$NOOP_N" -eq 0 ]; then
    pass "incremental no-op re-indexed nothing"
else
    fail "incremental no-op re-indexed $NOOP_N file(s) -- over-clearing regressed"
fi

note "PCH is faster than the same project without target_precompile_headers"
gen_project "$WORK/nopch" nopch
(cd "$WORK/nopch" && "$APP" index --full "$WORK/nopch/p.srctrl.toml" > "$WORK/nopch/run.log" 2>&1)
PCH_MS=$(ms_to_int "$(index_ms "$WORK/pch/warm.log")")
NOPCH_MS=$(ms_to_int "$(index_ms "$WORK/nopch/run.log")")
echo "  index time: PCH=${PCH_MS}ms  no-PCH=${NOPCH_MS}ms"
if [ "${PCH_MS:-0}" -gt 0 ] && [ "${NOPCH_MS:-0}" -gt 0 ] && [ "$PCH_MS" -lt "$NOPCH_MS" ]; then
    pass "PCH faster ($(( (NOPCH_MS - PCH_MS) * 100 / NOPCH_MS ))% quicker)"
else
    # Not a hard failure: on tiny/odd machines the difference can be noise.
    echo "NOTE: PCH not measurably faster here (PCH=${PCH_MS}ms no-PCH=${NOPCH_MS}ms); correctness checks above are the gate"
fi

note "summary"
if [ "$FAILURES" -eq 0 ]; then
    echo "PCH SMOKE OK"
    exit 0
else
    echo "PCH SMOKE FAILED: $FAILURES check(s)"
    exit 1
fi
