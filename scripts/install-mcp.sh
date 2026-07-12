#!/usr/bin/env bash
# Build and register the Sourcetrail MCP server (`sourcetrail-mcp`) with an MCP
# client (Claude Code by default). One server drives many app checkouts — it
# spawns/attaches instances via the start_instance / list_instances tools, each
# namespaced by its git label — so you install it ONCE, not per checkout. The
# connect-time handshake (get_instance_info) flags any protocol skew between this
# server and an older app build. See context/DESIGN_AGENT_UI_CONTROL.md
# (Protocol handshake, Registration & multi-checkout).
#
#   scripts/install-mcp.sh [options]
#
# Options:
#   --prefix DIR     Copy the built binary to DIR and register THAT path (stable,
#                    decoupled from this checkout's target/). Default: register the
#                    in-place target/release binary.
#   --name NAME      MCP server name to register as. Default: sourcetrail
#   --scope SCOPE    claude mcp scope: local | user | project. Default: user
#                    (available in every project, which suits one-server-many-checkouts).
#   --no-register    Build (and optionally copy) only; skip client registration.
#   --no-build       Skip the cargo build; use the existing binary.
#   -h, --help       Show this help.
#
# Requires: cargo (rustup toolchain). Optional: the `claude` CLI for auto-registration
# (otherwise a ready-to-paste config snippet is printed). macOS bash 3.2 compatible.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$REPO_ROOT/src/agent_mcp_bridge/Cargo.toml"

PREFIX=""
NAME="sourcetrail"
SCOPE="user"
DO_REGISTER=1
DO_BUILD=1

while [ $# -gt 0 ]; do
	case "$1" in
		--prefix) PREFIX="${2:?--prefix needs a directory}"; shift 2 ;;
		--name) NAME="${2:?--name needs a value}"; shift 2 ;;
		--scope) SCOPE="${2:?--scope needs a value}"; shift 2 ;;
		--no-register) DO_REGISTER=0; shift ;;
		--no-build) DO_BUILD=0; shift ;;
		-h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
	esac
done

if [ "$DO_BUILD" -eq 1 ]; then
	command -v cargo >/dev/null 2>&1 || { echo "error: cargo not found on PATH (install rustup)" >&2; exit 1; }
	echo ">> building sourcetrail-mcp (release, --features mcp)"
	cargo build --release --manifest-path "$MANIFEST" --bin sourcetrail-mcp --features mcp
fi

BIN="$REPO_ROOT/src/agent_mcp_bridge/target/release/sourcetrail-mcp"
[ -x "$BIN" ] || { echo "error: server binary not found at $BIN (run without --no-build)" >&2; exit 1; }

# Optionally copy to a stable location so registration does not pin to this checkout.
if [ -n "$PREFIX" ]; then
	mkdir -p "$PREFIX"
	cp "$BIN" "$PREFIX/sourcetrail-mcp"
	BIN="$PREFIX/sourcetrail-mcp"
	echo ">> installed binary at $BIN"
fi

echo ">> server binary: $BIN"

if [ "$DO_REGISTER" -eq 0 ]; then
	echo ">> --no-register: skipping client registration"
	exit 0
fi

if command -v claude >/dev/null 2>&1; then
	echo ">> registering with Claude Code as '$NAME' (scope: $SCOPE)"
	# Idempotent: drop any existing registration of the same name/scope first.
	claude mcp remove "$NAME" -s "$SCOPE" >/dev/null 2>&1 || true
	claude mcp add "$NAME" -s "$SCOPE" "$BIN"
	echo ">> done. Verify with: claude mcp get $NAME"
	echo ">> Open a NEW Claude Code session to pick up the tools."
else
	cat <<EOF
>> 'claude' CLI not found — register manually.

Claude Code:
    claude mcp add $NAME -s $SCOPE "$BIN"

Claude Desktop — add to
    ~/Library/Application Support/Claude/claude_desktop_config.json
(macOS) or %APPDATA%\\Claude\\claude_desktop_config.json (Windows):

    {
      "mcpServers": {
        "$NAME": {
          "command": "$BIN",
          "args": []
        }
      }
    }

Then restart the client.
EOF
fi
