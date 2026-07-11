#!/usr/bin/env bash
# Regression test for the [reserved-word-column] patch to sqlpp23-ddl2cpp.
# Requires python3 with pyparsing (pip install -r ../requirements.txt).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
script="$here/../sqlpp23-ddl2cpp"
out="$(mktemp -t ddl2cpp_test.XXXXXX.h)"
trap 'rm -f "$out"' EXIT

python3 "$script" \
	--path-to-ddl "$here/reserved_word.sql" \
	--path-to-header "$out" \
	--namespace t --assume-auto-id

fail=0
assert_has()    { grep -q "$1" "$out" || { echo "FAIL: expected /$1/"; fail=1; }; }
assert_absent() { grep -q "$1" "$out" && { echo "FAIL: unexpected /$1/"; fail=1; } || true; }

# Reserved-word columns must survive.
assert_has 'SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(key, key)'
assert_has 'SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(value, value)'
assert_has 'SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(check, check)'
# Real table constraints must NOT become columns.
assert_absent 'struct Primary'
assert_absent 'struct Foreign'

if [ "$fail" -ne 0 ]; then echo "reserved-word regression: FAILED"; exit 1; fi
echo "reserved-word regression: PASS"
