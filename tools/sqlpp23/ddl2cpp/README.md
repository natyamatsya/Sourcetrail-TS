# sqlpp23-ddl2cpp (vendored)

Code generator that turns SQL `CREATE TABLE` DDL into sqlpp23 table-model C++
headers. Used by the SQLite-storage migration to sqlpp23.

## Provenance

- **Upstream:** https://github.com/rbock/sqlpp23 — `scripts/sqlpp23-ddl2cpp`
- **Version:** tag `0.69`
- **Imported sha256:** `be2cadf48c8620e5984b4de9d49e26ac6a4f5288ad777aef960d83b18223f7b4`
- **License:** BSD-2-Clause (retained in the script header)

The initial commit is a byte-for-byte copy of the upstream script. Any local
changes are applied in **separate, clearly-labelled commits** so they can be
diffed against upstream and re-applied after a version bump.

## Installation

```bash
pip install -r requirements.txt   # pyparsing
```

## Usage

```bash
./sqlpp23-ddl2cpp --path-to-ddl schema.sql --path-to-header Tables.h \
                  --namespace mytables --assume-auto-id
```

## Local patches

Search the script for `LOCAL PATCH` to find every change vs. the pristine import.

### `[reserved-word-column]` — columns named after SQL keywords were silently dropped

A column literally named `key` (or `check`, `unique`, ...) was silently omitted
from the generated model, with no error. `key` is a SQL keyword, so the DDL
parser matched `key TEXT` as a table-level `KEY <expr>` constraint (which it
suppresses) instead of a column. For a key/value table like `meta`, the column
the table is keyed on simply vanished.

Fix: a table-level constraint keyword is never immediately followed by a data
type, so the constraint rule now has a negative lookahead (`~ddl_known_type`).
`key TEXT` → column; `PRIMARY KEY(...)` / `FOREIGN KEY(...)` → still constraints.

Regression test: `test/run_test.sh` (fixture `test/reserved_word.sql`).

```bash
pip install -r requirements.txt
./test/run_test.sh          # -> reserved-word regression: PASS
```

### Known limitation (not yet patched): `--assume-auto-id` on FK primary keys

`--assume-auto-id` marks every primary-key `id` as auto-increment. When a
table's `id` is itself a `FOREIGN KEY` to another table's id (junction tables),
that is wrong — the id must be supplied, not auto-assigned. ddl2cpp does not
cross-check FK constraints against the PK, so those columns must be corrected by
hand after generation (or via `--path-to-cpp-types`). See the `[PK-is-FK]` notes
in the migration's generated headers.
