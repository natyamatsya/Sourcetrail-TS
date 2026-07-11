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

_None yet in this commit (pristine import)._
