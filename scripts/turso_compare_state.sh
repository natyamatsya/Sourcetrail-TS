#!/usr/bin/env bash
# turso_compare_state.sh <sqlite.db> <turso.db>
#
# Authoritative data-equivalence check for the Turso comparison backend: compares
# the full content of every table in the two databases as an *unordered set*
# (dump -> sort -> hash), so it is immune to physical row-order differences
# between the engines. This is the comparison model that also survives an
# asynchronous / concurrent storage layer, where per-operation lockstep
# comparison is impossible.
#
# Turso writes SQLite-format files, so both databases are read with the same
# sqlite3 CLI (identical value rendering). The `meta` table is reported
# informationally only: Sourcetrail's temp-DB swap during indexing can leave the
# storage_version/timestamp rows split across the temp and final Turso files.
#
# Exit 0 when all non-meta tables match, 1 otherwise. macOS bash 3.2 compatible.
set -u

SQ="${1:?usage: turso_compare_state.sh <sqlite.db> <turso.db>}"
TU="${2:?usage: turso_compare_state.sh <sqlite.db> <turso.db>}"
[ -f "$SQ" ] || { echo "missing sqlite db: $SQ"; exit 2; }
[ -f "$TU" ] || { echo "missing turso db: $TU"; exit 2; }
command -v sqlite3 >/dev/null || { echo "sqlite3 not on PATH"; exit 2; }

WORK="$(mktemp -d /tmp/turso-cmp.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
SEP=$'\x1f'

# Tables = those present in the sqlite (source-of-truth) schema, minus sqlite
# internals. Order-independent content hash per table.
TABLES=$(sqlite3 "$SQ" "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name;")

printf "%-20s %10s %10s   %s\n" table sqlite turso result
printf -- "------------------------------------------------------------\n"

FAIL=0
for t in $TABLES; do
    sqlite3 -noheader -separator "$SEP" "$SQ" "SELECT * FROM \"$t\";" 2>/dev/null | LC_ALL=C sort > "$WORK/s.$t"
    sqlite3 -noheader -separator "$SEP" "$TU" "SELECT * FROM \"$t\";" 2>/dev/null | LC_ALL=C sort > "$WORK/t.$t"
    sc=$(wc -l < "$WORK/s.$t" | tr -d ' ')
    tc=$(wc -l < "$WORK/t.$t" | tr -d ' ')
    sh_s=$(shasum "$WORK/s.$t" | awk '{print $1}')
    sh_t=$(shasum "$WORK/t.$t" | awk '{print $1}')

    if [ "$sh_s" = "$sh_t" ]; then
        res="IDENTICAL"
    elif [ "$t" = "meta" ]; then
        res="meta (info only)"
    else
        res="DIFFERS"
        FAIL=$((FAIL + 1))
    fi
    printf "%-20s %10s %10s   %s\n" "$t" "$sc" "$tc" "$res"

    if [ "$sh_s" != "$sh_t" ]; then
        only_s=$(comm -23 "$WORK/s.$t" "$WORK/t.$t" | wc -l | tr -d ' ')
        only_t=$(comm -13 "$WORK/s.$t" "$WORK/t.$t" | wc -l | tr -d ' ')
        printf "    rows only-in-sqlite: %s   only-in-turso: %s\n" "$only_s" "$only_t"
        if [ "$t" != "meta" ]; then
            echo "    sample only-in-sqlite:"; comm -23 "$WORK/s.$t" "$WORK/t.$t" | head -2 | sed 's/^/      /'
            echo "    sample only-in-turso:";  comm -13 "$WORK/s.$t" "$WORK/t.$t" | head -2 | sed 's/^/      /'
        fi
    fi
done

printf -- "------------------------------------------------------------\n"
if [ "$FAIL" -eq 0 ]; then
    echo "RESULT: data IDENTICAL across all tables (meta informational)."
    exit 0
else
    echo "RESULT: $FAIL table(s) DIFFER."
    exit 1
fi
