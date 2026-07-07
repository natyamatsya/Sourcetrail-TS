#!/usr/bin/env bash
# Regression for incremental refresh, including flag-aware refresh (P1).
#
# Verifies, on a hermetic compilation-database fixture:
#   1. a no-op refresh re-indexes nothing ("Nothing to refresh")
#   2. touching a file without changing content re-indexes nothing
#   3. editing one .cpp re-indexes exactly that TU
#   4. editing a shared header re-indexes the dependent TUs (include-graph fanout)
#   5. a compile-FLAG change with no source change re-indexes the affected TU
#      (the flag-aware refresh -- previously a silently stale index)
#   6. the compile-command hash table is populated and updated
#
#   scripts/smoke-incremental.sh [build-dir]     (default: .build/llvm-clang-dbg)
set -u

BUILD_DIR="${1:-.build/llvm-clang-dbg}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$REPO_ROOT/$BUILD_DIR/app/Sourcetrail"
# A non-symlinked workspace: an indexed_header_paths under /tmp (-> /private/tmp)
# would not match clang's canonicalized header paths. Use the repo's build dir root.
WORK="$(mktemp -d "$REPO_ROOT/$BUILD_DIR/sourcetrail-inc-smoke.XXXXXX")"

FAILURES=0
note() { printf '\n=== %s ===\n' "$*"; }
pass() { printf 'PASS: %s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*"; FAILURES=$((FAILURES + 1)); }
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

[ -x "$APP" ] || { echo "missing $APP - build first"; exit 2; }
command -v sqlite3 >/dev/null || { echo "sqlite3 not found"; exit 2; }
pkill -9 -f 'sourcetrail_indexer|Sourcetrail index' 2>/dev/null

mkdir -p "$WORK/src"
cat > "$WORK/src/shared.h" <<'EOF'
#pragma once
inline int shared_helper(int x) { return x + 1; }
EOF
cat > "$WORK/src/a.cpp" <<'EOF'
#include "shared.h"
int computeA(int n) { return shared_helper(n); }
EOF
cat > "$WORK/src/b.cpp" <<'EOF'
#include "shared.h"
int computeB(int n) { return shared_helper(n) * 2; }
EOF
write_cdb() {  # $1 = extra flag for a.cpp (may be empty)
    cat > "$WORK/compile_commands.json" <<EOF
[
 { "directory": "$WORK/src", "command": "clang++ -std=c++17 $1 -c a.cpp", "file": "$WORK/src/a.cpp" },
 { "directory": "$WORK/src", "command": "clang++ -std=c++17 -c b.cpp", "file": "$WORK/src/b.cpp" }
]
EOF
}
write_cdb ""
cat > "$WORK/p.srctrl.toml" <<EOF
version = 8
[[source_groups]]
id = "11111111-2222-4333-8444-555555555555"
indexed_header_paths = ["$WORK/src"]
name = "inc"
status = "enabled"
type = "C/C++ from Compilation Database"
[source_groups.build_file_path]
compilation_db_path = "$WORK/compile_commands.json"
[source_groups.pch_flags]
use_compiler_flags = "0"
EOF
DB="$WORK/p.srctrl.db"

# Returns the number of source files re-indexed by the last run, or 0 for
# "Nothing to refresh".
indexed_count() {
    local log="$1"
    if grep -q 'Nothing to refresh' "$log"; then echo 0; return; fi
    grep -E 'Finished indexing' "$log" | tail -1 | sed -E 's#.*Finished indexing: ([0-9]+)/.*#\1#'
}
run() { (cd "$WORK" && "$APP" index "$@" "$WORK/p.srctrl.toml"); }

note "full index"
run --full > "$WORK/full.log" 2>&1
HASHES=$(sqlite3 "$DB" "SELECT COUNT(*) FROM file_command_hash;" 2>/dev/null)
[ "${HASHES:-0}" -eq 2 ] && pass "full index stored 2 command hashes" \
                         || fail "expected 2 command hashes, got '${HASHES}'"

note "no-op refresh re-indexes nothing"
run > "$WORK/noop.log" 2>&1
[ "$(indexed_count "$WORK/noop.log")" -eq 0 ] && pass "no-op re-indexed 0" \
    || { fail "no-op re-indexed $(indexed_count "$WORK/noop.log")"; }

note "touch without content change re-indexes nothing"
touch "$WORK/src/a.cpp"
run > "$WORK/touch.log" 2>&1
[ "$(indexed_count "$WORK/touch.log")" -eq 0 ] && pass "touch re-indexed 0" \
    || fail "touch re-indexed $(indexed_count "$WORK/touch.log")"

note "editing one .cpp re-indexes exactly it"
printf '#include "shared.h"\nint computeA(int n) { return shared_helper(n) + 7; }\n' > "$WORK/src/a.cpp"
run > "$WORK/edit.log" 2>&1
[ "$(indexed_count "$WORK/edit.log")" -eq 1 ] && pass "edit .cpp re-indexed 1" \
    || fail "edit .cpp re-indexed $(indexed_count "$WORK/edit.log") (expected 1)"

note "editing a shared header re-indexes dependents (fanout)"
printf '#pragma once\ninline int shared_helper(int x) { return x + 5; }\n' > "$WORK/src/shared.h"
run > "$WORK/hdr.log" 2>&1
[ "$(indexed_count "$WORK/hdr.log")" -eq 2 ] && pass "edit header re-indexed 2 (fanout)" \
    || fail "edit header re-indexed $(indexed_count "$WORK/hdr.log") (expected 2)"

note "flag change with NO source change re-indexes the affected TU (P1)"
write_cdb "-DNEW_DEFINE=1"
run > "$WORK/flag.log" 2>&1
if [ "$(indexed_count "$WORK/flag.log")" -eq 1 ]; then
    pass "flag change re-indexed 1 (flag-aware refresh works)"
else
    fail "flag change re-indexed $(indexed_count "$WORK/flag.log") (expected 1 -- flag change ignored?)"
fi

note "no-op after flag change re-indexes nothing (hash updated)"
run > "$WORK/noop2.log" 2>&1
[ "$(indexed_count "$WORK/noop2.log")" -eq 0 ] && pass "post-flag no-op re-indexed 0" \
    || fail "post-flag no-op re-indexed $(indexed_count "$WORK/noop2.log")"

note "summary"
if [ "$FAILURES" -eq 0 ]; then
    echo "INCREMENTAL SMOKE OK"
    exit 0
else
    echo "INCREMENTAL SMOKE FAILED: $FAILURES check(s)"
    exit 1
fi
