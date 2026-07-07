#!/usr/bin/env bash
# Acceptance test for distributed (sharded) indexing.
#
# Generates a hermetic multi-TU C++ fixture (two .cpp files that both include a
# shared header -- the case that produces duplicate cross-shard occurrences), then
# asserts:
#   shard 1/2 + shard 2/2 + merge  ==  a direct full index
# on node / edge / file / source_location / occurrence counts. Also checks the
# shard manifest and that merge rejects an inconsistent shard set.
#
#   scripts/smoke-distributed.sh [build-dir]     (default: .build/llvm-clang-dbg)
set -u

BUILD_DIR="${1:-.build/llvm-clang-dbg}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

APP_DIR="$REPO_ROOT/$BUILD_DIR/app"
APP="$APP_DIR/Sourcetrail"
WORK="$(mktemp -d /tmp/sourcetrail-shard-smoke.XXXXXX)"

FAILURES=0
note() { printf '\n=== %s ===\n' "$*"; }
pass() { printf 'PASS: %s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*"; FAILURES=$((FAILURES + 1)); }

counts() {
    # node edge file source_location occurrence
    sqlite3 "$1" \
        "SELECT (SELECT COUNT(*) FROM node) || ' ' ||
                (SELECT COUNT(*) FROM edge) || ' ' ||
                (SELECT COUNT(*) FROM file) || ' ' ||
                (SELECT COUNT(*) FROM source_location) || ' ' ||
                (SELECT COUNT(*) FROM occurrence);"
}

run_bounded() {  # <log> <seconds> <cmd...>  (cwd = WORK)
    local log="$1" deadline="$2"; shift 2
    (cd "$WORK" && "$@" > "$log" 2>&1) &
    local pid=$! waited=0
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$waited" -ge "$deadline" ]; then kill -9 "$pid"; return 124; fi
        sleep 1; waited=$((waited + 1))
    done
    wait "$pid"
}

[ -x "$APP" ] || { echo "missing $APP - build first"; exit 2; }
command -v sqlite3 >/dev/null || { echo "sqlite3 not found"; exit 2; }
pkill -9 -f 'sourcetrail_indexer|Sourcetrail index' 2>/dev/null

# ---------------------------------------------------------------- fixture
# A compilation-database source group (not a plain "C++ Source Group", which
# builds a bare invalid compiler invocation and indexes nothing). Two TUs both
# include shared.h so the merge exercises cross-shard node/location/occurrence
# dedup for real.
note "generate hermetic C++ fixture in $WORK"
mkdir -p "$WORK/src"

cat > "$WORK/src/shared.h" <<'EOF'
#pragma once
#include <string>
#include <vector>
namespace shared {
struct Widget {
    int value;
    std::string name;
    int doubled() const { return value * 2; }
};
inline int helper(int x) { return x + 1; }
inline std::vector<Widget> makeWidgets(int n) { return std::vector<Widget>(n); }
}
EOF

cat > "$WORK/src/a.cpp" <<'EOF'
#include "shared.h"
int computeA(int n) {
    shared::Widget w{n, "a"};
    auto ws = shared::makeWidgets(n);
    return shared::helper(w.doubled()) + static_cast<int>(ws.size());
}
EOF

cat > "$WORK/src/b.cpp" <<'EOF'
#include "shared.h"
int computeB(int n) {
    shared::Widget w{n + 10, "b"};
    auto ws = shared::makeWidgets(n);
    return shared::helper(w.value) + w.doubled() + static_cast<int>(ws.size());
}
EOF

cat > "$WORK/compile_commands.json" <<EOF
[
  { "directory": "$WORK/src", "command": "clang++ -std=c++17 -c a.cpp", "file": "$WORK/src/a.cpp" },
  { "directory": "$WORK/src", "command": "clang++ -std=c++17 -c b.cpp", "file": "$WORK/src/b.cpp" }
]
EOF

cat > "$WORK/fixture.srctrl.toml" <<'EOF'
version = 8

[[source_groups]]
id = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"
indexed_header_paths = ["./src"]
name = "Shard Fixture (Compilation Database)"
status = "enabled"
type = "C/C++ from Compilation Database"

[source_groups.build_file_path]
compilation_db_path = "./compile_commands.json"

[source_groups.pch_flags]
use_compiler_flags = "0"
EOF

FIXTURE="fixture.srctrl.toml"           # relative to WORK
BASELINE_DB="$WORK/fixture.srctrl.db"
pass "fixture written (2 TUs sharing shared.h)"

# ---------------------------------------------------------------- baseline
note "baseline: direct full index"
if run_bounded "$WORK/baseline.log" 180 "$APP" index --full "$FIXTURE"; then
    BASELINE_COUNTS=$(counts "$BASELINE_DB")
    NODES=$(echo "$BASELINE_COUNTS" | cut -d' ' -f1)
    if [ "${NODES:-0}" -gt 0 ]; then
        pass "baseline indexed (node/edge/file/loc/occ: $BASELINE_COUNTS)"
    else
        fail "baseline produced 0 nodes -- fixture did not index (see $WORK/baseline.log)"
        tail -15 "$WORK/baseline.log"; exit 1
    fi
    # Clean-toolchain guard: the fixture includes <string>/<vector>, so a broken
    # macOS sysroot setup (missing -isysroot, or a stale SDK usr/include breaking
    # libc++ #include_next) surfaces here as header-not-found errors.
    ERR=$(sqlite3 "$BASELINE_DB" "SELECT COUNT(*) FROM error;" 2>/dev/null)
    if [ "${ERR:-1}" -eq 0 ]; then
        pass "clean toolchain (0 indexing errors on STL-using fixture)"
    else
        fail "toolchain not clean: $ERR indexing error(s) -- check -isysroot injection / stale SDK paths"
        sqlite3 "$BASELINE_DB" "SELECT DISTINCT substr(message,1,70) FROM error LIMIT 6;" 2>/dev/null | sed 's/^/     /'
    fi
else
    fail "baseline index failed (see $WORK/baseline.log)"; tail -15 "$WORK/baseline.log"; exit 1
fi

# ---------------------------------------------------------------- shards
note "shard runs (1/2 and 2/2)"
for i in 1 2; do
    if run_bounded "$WORK/shard$i.log" 180 \
        "$APP" index --full --shard "$i/2" --shard-output "$WORK/shard$i.db" "$FIXTURE"; then
        if [ -f "$WORK/shard$i.db" ]; then
            MI=$(sqlite3 "$WORK/shard$i.db" "SELECT value FROM meta WHERE key='shard_index';" 2>/dev/null)
            MC=$(sqlite3 "$WORK/shard$i.db" "SELECT value FROM meta WHERE key='shard_count';" 2>/dev/null)
            SN=$(sqlite3 "$WORK/shard$i.db" "SELECT COUNT(*) FROM node;" 2>/dev/null)
            [ "$MI" = "$i" ] && [ "$MC" = "2" ] \
                && pass "shard $i/2 produced (manifest ok, $SN nodes)" \
                || fail "shard $i/2 manifest wrong: index='$MI' count='$MC'"
        else
            fail "shard $i/2 DB missing"
        fi
    else
        fail "shard $i/2 run failed (see $WORK/shard$i.log)"
    fi
done

# ---------------------------------------------------------------- guard
# The CLI does not distinguish process exit codes (parse() only sets an internal
# quit flag), so assert on the diagnostic + the fact that no output DB is written.
note "merge guard: inconsistent shard set (same stripe twice) must be rejected"
rm -f "$WORK/reject.db"
(cd "$WORK" && "$APP" merge "$FIXTURE" "$WORK/shard1.db" "$WORK/shard1.db" \
    --output "$WORK/reject.db" > "$WORK/guard.log" 2>&1)
if grep -q 'inconsistent or incomplete' "$WORK/guard.log" && [ ! -f "$WORK/reject.db" ]; then
    pass "merge rejected inconsistent shard set (no output DB written)"
else
    fail "merge did not reject inconsistent shard set"; tail -5 "$WORK/guard.log"
fi

# ---------------------------------------------------------------- merge
note "merge shards"
if (cd "$WORK" && "$APP" merge "$FIXTURE" "$WORK/shard1.db" "$WORK/shard2.db" \
        --output "$WORK/merged.db" > "$WORK/merge.log" 2>&1); then
    grep -E 'merge complete' "$WORK/merge.log"
    MERGED_COUNTS=$(counts "$WORK/merged.db")
    if [ "$MERGED_COUNTS" = "$BASELINE_COUNTS" ]; then
        pass "merged counts equal baseline ($MERGED_COUNTS)"
    else
        fail "count mismatch: baseline='$BASELINE_COUNTS' merged='$MERGED_COUNTS'"
    fi
else
    fail "merge failed (see $WORK/merge.log)"; tail -5 "$WORK/merge.log"
fi

# ---------------------------------------------------------------- summary
note "summary"
if [ "$FAILURES" -eq 0 ]; then
    echo "DISTRIBUTED SMOKE OK (logs in $WORK)"
    exit 0
else
    echo "DISTRIBUTED SMOKE FAILED: $FAILURES check(s) (logs in $WORK)"
    exit 1
fi
