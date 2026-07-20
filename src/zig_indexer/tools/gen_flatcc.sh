#!/usr/bin/env bash
# Generate the flatcc C bindings for the Sourcetrail IPC schemas.
#
# flatcc's parser rejects the non-ASCII characters (e.g. "§") that appear in the
# shared .fbs comments — flatc (used by the C++/Rust/Swift sides) tolerates them.
# So strip non-ASCII into a temp copy for flatcc only; the source schemas are
# never modified.
#
# Usage: gen_flatcc.sh <schema_dir> <out_dir>
set -euo pipefail

SCHEMA_DIR="${1:?usage: gen_flatcc.sh <schema_dir> <out_dir>}"
OUT_DIR="${2:?usage: gen_flatcc.sh <schema_dir> <out_dir>}"

FBS=(indexer_command intermediate_storage indexing_status)

mkdir -p "$OUT_DIR/fbs"
for f in "${FBS[@]}"; do
	LC_ALL=C tr -cd '\0-\177' < "$SCHEMA_DIR/$f.fbs" > "$OUT_DIR/fbs/$f.fbs"
done

flatcc -a -o "$OUT_DIR" \
	"$OUT_DIR/fbs/indexer_command.fbs" \
	"$OUT_DIR/fbs/intermediate_storage.fbs" \
	"$OUT_DIR/fbs/indexing_status.fbs"
