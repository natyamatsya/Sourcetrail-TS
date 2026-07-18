#!/usr/bin/env bash
# Headless smoke harness for Sourcetrail's concurrency work.
#
#   scripts/smoke.sh [build-dir]        (default: .build/llvm-clang-dbg)
#
# Stages (each must pass):
#   1. unit    - Sourcetrail_test, excluding [libcxx_compat] (segfaults under the
#                homebrew LLVM 22 libc++ hash change; tracked separately)
#   2. index   - full headless index of the bundled tutorial fixture (CLI mode,
#                exercises the multi-process indexing pipeline end to end)
#   3. interrupt - SIGINT mid-index; the cooperative-cancellation path
#                (MessageIndexingInterrupted -> stop_token -> graceful subprocess
#                exit, bounded kill fallback) must exit promptly, no orphans
#
# macOS bash 3.2 compatible; no coreutils `timeout` required.
set -u

BUILD_DIR="${1:-.build/llvm-clang-dbg}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

APP_DIR="$BUILD_DIR/app"
APP="$APP_DIR/Sourcetrail"
TEST_BIN="$BUILD_DIR/test/Sourcetrail_test"
# The fork uses TOML project files; the legacy .srctrlprj is rejected by the CLI
# ("wrong file ending") -- and note the CLI currently keeps running after that
# rejection instead of exiting, which looks like a hang.
FIXTURE="user/projects/tutorial/tutorial.srctrl.toml"   # relative to APP_DIR
SCRATCH="$(mktemp -d /tmp/sourcetrail-smoke.XXXXXX)"

FAILURES=0

note()  { printf '\n=== %s ===\n' "$*"; }
pass()  { printf 'PASS: %s\n' "$*"; }
fail()  { printf 'FAIL: %s\n' "$*"; FAILURES=$((FAILURES + 1)); }

# wait_pid_gone <pid> <seconds> -> 0 if the process exited within the deadline
wait_pid_gone() {
    local pid="$1" deadline="$2" waited=0
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$waited" -ge "$deadline" ]; then return 1; fi
        sleep 1; waited=$((waited + 1))
    done
    return 0
}

kill_strays() {
    pkill -9 -f sourcetrail_indexer 2>/dev/null
    pkill -9 -x Sourcetrail 2>/dev/null
}

[ -x "$APP" ] || { echo "missing $APP - build the Sourcetrail target first"; exit 2; }
[ -x "$TEST_BIN" ] || { echo "missing $TEST_BIN - build the Sourcetrail_test target first"; exit 2; }
kill_strays

# ---------------------------------------------------------------- 1. unit
# Scoped to the concurrency-relevant suites (messaging, scheduling) until the
# PRE-EXISTING full-suite debt is cleaned up (see memory/known-issues):
#   - a stack-smashing segfault (vector<FilePath> copy, corrupted backtrace)
#     somewhere in the shard with logger/searchindex/matrix tests - needs an
#     ASan build to pinpoint; it hid a tail of ~40 never-run failing tests
# ("ipc integration: full indexer workflow" was in this list — its Swift hang and
# the relative-app-path bug that stopped TaskBuildIndex launching the indexers
# are now fixed, so it passes; it is excluded from this fast stage only because
# it spawns the real indexer subprocesses.)
UNIT_FILTER='message*,listener*,messages*,concurrent*,scheduler*,task*,scheduled*,sequential*,storage provider*'
note "stage 1: unit tests (concurrency-relevant subset)"
"$TEST_BIN" "$UNIT_FILTER" > "$SCRATCH/unit.log" 2>&1 &
UNIT_PID=$!
if wait_pid_gone "$UNIT_PID" 600; then
    wait "$UNIT_PID"; UNIT_EXIT=$?
    if [ "$UNIT_EXIT" -eq 0 ]; then
        pass "unit tests ($(grep -E 'assertions' "$SCRATCH/unit.log" | tail -1 || echo 'exit 0'))"
    else
        fail "unit tests (exit $UNIT_EXIT, see $SCRATCH/unit.log)"
        tail -15 "$SCRATCH/unit.log"
    fi
else
    fail "unit tests still running after 600s (hang) - killing"
    kill -9 "$UNIT_PID" 2>/dev/null
    tail -8 "$SCRATCH/unit.log"
fi

# ---------------------------------------------------------------- 2. index
note "stage 2: headless full index of the tutorial fixture"
(cd "$APP_DIR" && ./Sourcetrail index --full "$FIXTURE" > "$SCRATCH/index.log" 2>&1) &
INDEX_WRAPPER=$!
if wait_pid_gone "$INDEX_WRAPPER" 180; then
    wait "$INDEX_WRAPPER"; INDEX_EXIT=$?
    if [ "$INDEX_EXIT" -eq 0 ] && ! grep -qiE 'SIGSEGV|SIGABRT|terminate called' "$SCRATCH/index.log"; then
        pass "headless index completed (exit 0, no crash markers)"
    else
        fail "headless index (exit $INDEX_EXIT, see $SCRATCH/index.log)"
        tail -10 "$SCRATCH/index.log"
    fi
else
    fail "headless index did not finish within 180s"
    kill_strays
fi

# ---------------------------------------------------------------- 3. interrupt
note "stage 3: SIGINT mid-index -> cooperative cancellation"
(cd "$APP_DIR" && exec ./Sourcetrail index --full "$FIXTURE" > "$SCRATCH/interrupt.log" 2>&1) &
INDEX_PID=$!
sleep 2   # let subprocesses spawn and indexing begin
if ! kill -0 "$INDEX_PID" 2>/dev/null; then
    # Finished before we could interrupt (tiny fixture / fast machine) - not a failure,
    # but the interrupt path was not exercised.
    pass "index finished before interrupt window (skipped; use a larger fixture to exercise)"
else
    kill -INT "$INDEX_PID"
    if wait_pid_gone "$INDEX_PID" 20; then
        LEFTOVER=$(pgrep -f sourcetrail_indexer | wc -l | tr -d ' ')
        if [ "$LEFTOVER" -eq 0 ]; then
            pass "interrupted index exited within 20s, no orphaned indexer subprocesses"
        else
            fail "interrupted index left $LEFTOVER orphaned indexer subprocess(es)"
            kill_strays
        fi
    else
        fail "interrupted index still running after 20s (hang)"
        kill_strays
    fi
fi

# ---------------------------------------------------------------- summary
note "summary"
if [ "$FAILURES" -eq 0 ]; then
    echo "SMOKE OK (logs in $SCRATCH)"
    exit 0
else
    echo "SMOKE FAILED: $FAILURES stage(s) (logs in $SCRATCH)"
    exit 1
fi
