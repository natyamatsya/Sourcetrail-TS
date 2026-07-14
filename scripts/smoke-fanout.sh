#!/usr/bin/env bash
# Fan-out smoke (DESIGN_MULTIGROUP_FANOUT.md S4): index the same generated C++
# sources once as ONE source group (serial SQLite baseline) and once as TWO
# source groups (per-group subprocess clusters + concurrent Turso sole writer +
# Turso->SQLite export), then compare per-table row counts. Counts must match
# exactly — dedup/id assignment is order-independent even though ids differ.
#
#   scripts/smoke-fanout.sh [build-dir]     (default: .build/llvm-clang-dbg)
#
# Requires a build with SOURCETRAIL_TURSO_CONCURRENT=ON (asserted via the log
# markers: the run must report the SOLE-writer mode and the export).
# macOS bash 3.2 compatible.
set -u

BUILD_DIR="${1:-.build/llvm-clang-dbg}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

APP_DIR="$BUILD_DIR/app"
APP="$APP_DIR/Sourcetrail"
[ -x "$APP" ] || { echo "missing $APP - build the Sourcetrail target first"; exit 2; }
command -v sqlite3 >/dev/null || { echo "sqlite3 not on PATH"; exit 2; }

SCRATCH="$(mktemp -d /tmp/sourcetrail-fanout.XXXXXX)"
FAILURES=0
note()  { printf '\n=== %s ===\n' "$*"; }
pass()  { printf 'PASS: %s\n' "$*"; }
fail()  { printf 'FAIL: %s\n' "$*"; FAILURES=$((FAILURES + 1)); }

kill_strays() {
    pkill -9 -f sourcetrail_indexer 2>/dev/null
    pkill -9 -x Sourcetrail 2>/dev/null
}

wait_pid_gone() {
    local pid="$1" deadline="$2" waited=0
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$waited" -ge "$deadline" ]; then return 1; fi
        sleep 1; waited=$((waited + 1))
    done
    return 0
}

# ------------------------------------------------ fixture generation
# Two directories of self-contained C++ files. Every file declares and calls
# the shared `common_fn` (defined once in group a), so a symbol referenced from
# BOTH groups exercises the cross-group dedup of the shared writer.
FILES_PER_GROUP=30
FUNCS_PER_FILE=6

gen_group() {
    local dir="$1" tag="$2" defines_common="$3"
    mkdir -p "$dir"
    local i f
    i=0
    while [ "$i" -lt "$FILES_PER_GROUP" ]; do
        {
            echo "int common_fn(int x);"
            echo "namespace grp_${tag} {"
            echo "int helper_${tag}_${i}_0(int x) { return x + ${i}; }"
            f=1
            while [ "$f" -lt "$FUNCS_PER_FILE" ]; do
                echo "int helper_${tag}_${i}_${f}(int x) { return helper_${tag}_${i}_$((f - 1))(x) * 2; }"
                f=$((f + 1))
            done
            echo "}"
            echo "int use_common_${tag}_${i}(int x) { return common_fn(x) + grp_${tag}::helper_${tag}_${i}_$((FUNCS_PER_FILE - 1))(x); }"
            if [ "$defines_common" = "yes" ] && [ "$i" -eq 0 ]; then
                echo "int common_fn(int x) { return x * 42; }"
            fi
        } > "$dir/file_${tag}_${i}.cpp"
        i=$((i + 1))
    done
}

note "generating fixture ($((FILES_PER_GROUP * 2)) files)"
SRC_A="$SCRATCH/src_a"
SRC_B="$SCRATCH/src_b"
gen_group "$SRC_A" a yes
gen_group "$SRC_B" b no

# Project writers. The single-group baseline spans both dirs in ONE group;
# the fan-out project holds them as TWO groups.
write_group() {
    local id="$1" name="$2"; shift 2
    echo "[[source_groups]]"
    echo "cpp_standard = 'c++17'"
    echo "id = '$id'"
    echo "name = '$name'"
    echo "status = 'enabled'"
    echo "type = 'C++ Source Group'"
    echo ""
    echo "    [source_groups.source_extensions]"
    echo "    source_extension = '.cpp'"
    echo ""
    echo "    [source_groups.source_paths]"
    if [ "$#" -eq 1 ]; then
        echo "    source_path = '$1'"
    else
        echo "    source_path = ['$1', '$2']"
    fi
    echo ""
}

BASE_DIR="$SCRATCH/baseline"; mkdir -p "$BASE_DIR"
{
    echo "version = '8'"
    echo ""
    write_group "aaaaaaaa-0000-0000-0000-000000000001" "All Sources" "$SRC_A" "$SRC_B"
} > "$BASE_DIR/baseline.srctrl.toml"

FAN_DIR="$SCRATCH/fanout"; mkdir -p "$FAN_DIR"
{
    echo "version = '8'"
    echo ""
    write_group "bbbbbbbb-0000-0000-0000-00000000000a" "Group A" "$SRC_A"
    write_group "bbbbbbbb-0000-0000-0000-00000000000b" "Group B" "$SRC_B"
} > "$FAN_DIR/fanout.srctrl.toml"

# ------------------------------------------------ index runs
run_index() {
    local name="$1" project="$2" log="$3"
    note "indexing: $name"
    (cd "$APP_DIR" && ./Sourcetrail index --full "$project" > "$log" 2>&1) &
    local wrapper=$!
    if ! wait_pid_gone "$wrapper" 300; then
        fail "$name: did not finish within 300s"
        kill_strays
        return 1
    fi
    wait "$wrapper"; local code=$?
    if [ "$code" -ne 0 ] || grep -qiE 'SIGSEGV|SIGABRT|terminate called' "$log"; then
        fail "$name: exit $code (see $log)"
        tail -10 "$log"
        return 1
    fi
    pass "$name: indexed (exit 0)"
    return 0
}

run_index "serial baseline (1 group)" "$BASE_DIR/baseline.srctrl.toml" "$SCRATCH/baseline.log" || true
run_index "fan-out (2 groups)" "$FAN_DIR/fanout.srctrl.toml" "$SCRATCH/fanout.log" || true

BASE_DB="$BASE_DIR/baseline.srctrl.db"
FAN_DB="$FAN_DIR/fanout.srctrl.db"
[ -f "$BASE_DB" ] || fail "baseline db missing: $BASE_DB"
[ -f "$FAN_DB" ] || fail "fan-out db missing: $FAN_DB"

# ------------------------------------------------ fan-out path engaged?
note "fan-out path markers"
FANOUT_LOG_DIR="$APP_DIR/user/log"
LATEST_LOG="$(ls -t "$FANOUT_LOG_DIR"/log_*.txt 2>/dev/null | head -1)"
check_marker() {
    local what="$1" pattern="$2"
    if [ -n "$LATEST_LOG" ] && grep -q "$pattern" "$LATEST_LOG"; then
        pass "$what"
    elif grep -q "$pattern" "$SCRATCH/fanout.log" 2>/dev/null; then
        pass "$what (stdout)"
    else
        fail "$what: marker '$pattern' not found (log: ${LATEST_LOG:-none})"
    fi
}
check_marker "cluster plan active" "indexer cluster plan: source group"
check_marker "sole-writer mode engaged" "SOLE ingest writer"
check_marker "export ran" "Turso->SQLite export:"

# ------------------------------------------------ count comparison
note "per-table count comparison (baseline vs fan-out)"
TABLES="element node edge symbol file filecontent local_symbol source_location occurrence component_access element_component error"
for t in $TABLES; do
    B=$(sqlite3 "$BASE_DB" "SELECT COUNT(*) FROM $t;" 2>/dev/null || echo "ERR")
    F=$(sqlite3 "$FAN_DB" "SELECT COUNT(*) FROM $t;" 2>/dev/null || echo "ERR")
    if [ "$B" = "$F" ] && [ "$B" != "ERR" ]; then
        pass "$t: $B == $F"
    else
        fail "$t: baseline=$B fan-out=$F"
    fi
done

# Sanity: something was actually indexed.
NODES=$(sqlite3 "$FAN_DB" "SELECT COUNT(*) FROM node;" 2>/dev/null || echo 0)
if [ "${NODES:-0}" -gt 100 ]; then
    pass "fan-out db holds a real graph ($NODES nodes)"
else
    fail "fan-out db suspiciously small ($NODES nodes)"
fi

# The per-run turso ingest scratch file must be cleaned up after the export.
if ls "$FAN_DIR"/*.concurrent.turso* >/dev/null 2>&1; then
    fail "leftover .concurrent.turso ingest file(s) in $FAN_DIR"
else
    pass "no leftover .concurrent.turso ingest files"
fi

# ------------------------------------------------ summary
note "summary"
if [ "$FAILURES" -eq 0 ]; then
    echo "FAN-OUT SMOKE OK (scratch: $SCRATCH)"
    exit 0
else
    echo "FAN-OUT SMOKE FAILED: $FAILURES check(s) (scratch: $SCRATCH)"
    exit 1
fi
