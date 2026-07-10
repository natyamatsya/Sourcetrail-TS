#!/usr/bin/env bash
# Headless end-to-end smoke for the Rust indexer: the indexer indexes its own
# crate (src/rust_indexer, incl. the thoth-ipc path dependency) through the
# real app pipeline (CLI -> IPC -> subprocess -> PersistentStorage), then the
# generated database is verified with sqlite3.
#
#   scripts/smoke-rust.sh [build-dir]        (default: .build/llvm-clang-dbg)
#
# Requires: app + sourcetrail_rust_indexer built with
# BUILD_RUST_LANGUAGE_PACKAGE=ON (cmake --build <dir> --target Sourcetrail
# copy_rust_indexer), sqlite3 on PATH.
#
# Asserts (see context/DESIGN_RUST_TYPE_SYSTEM_EDGES.md):
#   - index exits 0, no crash markers, 0 errors recorded in the DB
#   - node kinds present: struct, method, trait, type parameter
#   - edge kinds present: MEMBER, TYPE_USAGE, USAGE, CALL, INHERITANCE,
#     OVERRIDE, TYPE_ARGUMENT
#   - reference occurrences attach to edges; definitions have SCOPE locations
#
# macOS bash 3.2 compatible.
set -u

BUILD_DIR="${1:-.build/llvm-clang-dbg}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

APP_DIR="$BUILD_DIR/app"
APP="$APP_DIR/Sourcetrail"
CRATE_DIR="$REPO_ROOT/src/rust_indexer"        # workspace root (Cargo.toml)
PROJECT="$CRATE_DIR/smoke_rust.srctrl.toml"    # project dir == crate root
DB="$CRATE_DIR/smoke_rust.srctrl.db"
SCRATCH="$(mktemp -d /tmp/sourcetrail-smoke-rust.XXXXXX)"

FAILURES=0
note() { printf '\n=== %s ===\n' "$*"; }
pass() { printf 'PASS: %s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*"; FAILURES=$((FAILURES + 1)); }

cleanup() {
    rm -f "$PROJECT" "$DB" "$DB"-* "$DB"_tmp "$DB"_tmp-* \
        "$CRATE_DIR/smoke_rust.srctrl.bm"
}

wait_pid_gone() {
    local pid="$1" deadline="$2" waited=0
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$waited" -ge "$deadline" ]; then return 1; fi
        sleep 1; waited=$((waited + 1))
    done
    return 0
}

[ -x "$APP" ] || { echo "missing $APP - build the Sourcetrail target first"; exit 2; }
[ -x "$APP_DIR/sourcetrail_rust_indexer" ] || {
    echo "missing $APP_DIR/sourcetrail_rust_indexer - build copy_rust_indexer first"; exit 2; }
command -v sqlite3 >/dev/null || { echo "sqlite3 not found on PATH"; exit 2; }
pkill -9 -f sourcetrail_rust_indexer 2>/dev/null
cleanup

# ------------------------------------------------------------- 1. index
note "stage 1: headless self-index (crate root: src/rust_indexer)"
cat > "$PROJECT" <<'EOF'
version = '8'

[[source_groups]]
id = 'a3f1c2e4-0000-4000-8000-smokerust0001'
name = 'Rust Smoke'
status = 'enabled'
type = 'Rust Empty'

    [source_groups.source_extensions]
    source_extension = '.rs'

    [source_groups.source_paths]
    source_path = './indexer/src'
EOF

(cd "$APP_DIR" && ./Sourcetrail index --full "$PROJECT" > "$SCRATCH/index.log" 2>&1) &
INDEX_WRAPPER=$!
if wait_pid_gone "$INDEX_WRAPPER" 300; then
    wait "$INDEX_WRAPPER"; INDEX_EXIT=$?
    if [ "$INDEX_EXIT" -eq 0 ] && ! grep -qiE 'SIGSEGV|SIGABRT|terminate called' "$SCRATCH/index.log"; then
        pass "headless index completed (exit 0, no crash markers)"
    else
        fail "headless index (exit $INDEX_EXIT, log: $SCRATCH/index.log)"
        tail -15 "$SCRATCH/index.log"
    fi
else
    fail "headless index did not finish within 300s"
    pkill -9 -f sourcetrail_rust_indexer 2>/dev/null
    pkill -9 -x Sourcetrail 2>/dev/null
fi

# ------------------------------------------------------------- 2. verify DB
note "stage 2: verify $DB"
if [ ! -f "$DB" ]; then
    fail "database was not created"
    echo "artifacts kept in $SCRATCH and $CRATE_DIR for inspection"
    exit 1
fi

q() { sqlite3 "$DB" "$1"; }

# expect <label> <sql> — passes when the query returns a value >= 1
expect() {
    local label="$1" sql="$2" n
    n=$(q "$sql")
    if [ "${n:-0}" -ge 1 ]; then
        pass "$label ($n)"
    else
        fail "$label (got ${n:-0})"
    fi
}

ERRORS=$(q "SELECT count(*) FROM error;")
if [ "$ERRORS" -eq 0 ]; then
    pass "0 errors recorded"
else
    fail "$ERRORS errors recorded:"
    q "SELECT message FROM error LIMIT 5;"
fi

# Node kinds (NodeKind.h bitmask values)
expect "struct nodes (64)"          "SELECT count(*) FROM node WHERE type = 64;"
expect "trait nodes (256)"          "SELECT count(*) FROM node WHERE type = 256;"
expect "method nodes (8192)"        "SELECT count(*) FROM node WHERE type = 8192;"
expect "type-parameter nodes (131072)" "SELECT count(*) FROM node WHERE type = 131072;"
expect "known symbol collect_semantic_edges" \
    "SELECT count(*) FROM node WHERE serialized_name LIKE '%collect_semantic_edges%';"

# Edge kinds (Edge.h bitmask values)
expect "MEMBER edges (1)"           "SELECT count(*) FROM edge WHERE type = 1;"
expect "TYPE_USAGE edges (2)"       "SELECT count(*) FROM edge WHERE type = 2;"
expect "USAGE edges (4)"            "SELECT count(*) FROM edge WHERE type = 4;"
expect "CALL edges (8)"             "SELECT count(*) FROM edge WHERE type = 8;"
expect "INHERITANCE edges (16)"     "SELECT count(*) FROM edge WHERE type = 16;"
expect "OVERRIDE edges (32)"        "SELECT count(*) FROM edge WHERE type = 32;"
expect "TYPE_ARGUMENT edges (64)"   "SELECT count(*) FROM edge WHERE type = 64;"

# Reference occurrences attach to edges (recordReference model)
expect "occurrences on edges" \
    "SELECT count(*) FROM occurrence o JOIN edge e ON o.element_id = e.id;"
# Definitions carry SCOPE locations (type 1) for snippet extents
expect "SCOPE locations (type 1)" \
    "SELECT count(*) FROM source_location WHERE type = 1;"
# Macro-expanded items (e.g. #[derive] methods) land as IMPLICIT definitions
expect "implicit (macro-generated) definitions" \
    "SELECT count(*) FROM symbol WHERE definition_kind = 1;"
# Bounds originate at parameter nodes: a type-parameter node with an
# outgoing TYPE_USAGE edge must exist
expect "bound edge from a type-parameter node" \
    "SELECT count(*) FROM edge e JOIN node n ON e.source_node_id = n.id
     WHERE e.type = 2 AND n.type = 131072;"

# ------------------------------------------------------------- summary
note "summary"
q "SELECT 'nodes: ' || count(*) FROM node;
   SELECT 'edges: ' || count(*) FROM edge;
   SELECT 'occurrences: ' || count(*) FROM occurrence;
   SELECT 'source locations: ' || count(*) FROM source_location;"

if [ "$FAILURES" -eq 0 ]; then
    echo "ALL PASS"
    cleanup
    rm -rf "$SCRATCH"
    exit 0
else
    echo "$FAILURES FAILURE(S) — artifacts kept: $DB, $SCRATCH"
    exit 1
fi
