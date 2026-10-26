#!/usr/bin/env python3
"""Drive a sourcetrail-mcp build over stdio, without an MCP client.

    mcp_call.py [--images DIR] [--log FILE] [--full] SERVER CALLS

Tests a bridge you just built (or changed) before any MCP client loads it: SERVER is
the binary (target/release/sourcetrail-mcp), CALLS what to call on it:

    '[["instance_start", {"id": "t1", "bin": ".../app/Sourcetrail"}], ["ui_state", {"instance": "t1"}]]'
    @calls.json     the same list, from a file
    -               one [tool, args] per stdin line, each answered as it arrives

The bridge stops instances it started when it exits, so a session that explores step
by step keeps one bridge running with `-`, fed from a file it appends calls to:

    tail -n +1 -f calls.jsonl | mcp_call.py --images shots SERVER - > replies.txt &
    echo '["ui_frame", {"instance": "t1"}]' >> calls.jsonl

Each reply prints as `== tool args ok|refused|ERROR` and its text (cut at 1500
characters unless --full): refused is a call the app turned down ({"ok": false}, e.g.
an event its state does not accept), ERROR one that failed. Images (ui_frame,
ui_capture) are written to --images as PNG files and printed as their path; the
bridge's own log goes to --log instead of stderr. Exits 1 if any call failed.
"""
import argparse
import base64
import json
import os
import subprocess
import sys


class Bridge:
    def __init__(self, server, log):
        self.proc = subprocess.Popen([server], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, text=True)
        self.next_id = 0

    def send(self, message):
        self.proc.stdin.write(json.dumps(message) + "\n")
        self.proc.stdin.flush()

    def request(self, method, params):
        self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params})
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise SystemExit(f"the bridge closed its stdout (exit code {self.proc.poll()})")
            try:
                message = json.loads(line)
            except json.JSONDecodeError:
                print(f"(not JSON-RPC on stdout: {line.rstrip()[:200]})", file=sys.stderr)
                continue
            # A request of the server's own carries an id too: only a response answers ours.
            if "method" not in message and message.get("id") == self.next_id:
                return message

    def close(self):
        self.proc.stdin.close()
        try:
            self.proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def refused(parts):
    """Whether the reply's JSON says the app turned the call down."""
    for part in parts:
        try:
            value = json.loads(part)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict) and value.get("ok") is False:
            return True
    return False


def calls_from(spec):
    if spec == "-":
        for line in sys.stdin:
            if line.strip():
                yield json.loads(line)
        return
    if spec.startswith("@"):
        with open(spec[1:], encoding="utf-8") as file:
            spec = file.read()
    yield from json.loads(spec)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("server", help="the sourcetrail-mcp binary")
    parser.add_argument("calls", help="a JSON list of [tool, args], @file with one, or - for stdin lines")
    parser.add_argument("--images", metavar="DIR", help="write image results here as PNG files")
    parser.add_argument("--log", metavar="FILE", help="append the bridge's log (its stderr) here")
    parser.add_argument("--full", action="store_true", help="print replies in full (MCP_CALL_FULL=1 too)")
    options = parser.parse_args()
    limit = None if options.full or os.environ.get("MCP_CALL_FULL") else 1500
    if options.images:
        os.makedirs(options.images, exist_ok=True)

    log = open(options.log, "a", encoding="utf-8") if options.log else None
    bridge = Bridge(options.server, log)
    init = bridge.request(
        "initialize",
        {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "mcp_call", "version": "1"}})
    if "error" in init:
        raise SystemExit(f"initialize failed: {json.dumps(init['error'])}")
    bridge.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
    print("server:", init["result"]["serverInfo"], flush=True)

    failed = False
    images = 0
    for tool, args in calls_from(options.calls):
        reply = bridge.request("tools/call", {"name": tool, "arguments": args})
        result = reply.get("result", {})
        error = "error" in reply or bool(result.get("isError"))
        failed |= error
        parts = []
        for content in result.get("content", []):
            if content.get("type") == "image" and options.images:
                images += 1
                path = os.path.join(options.images, f"{images:03}-{tool}.png")
                with open(path, "wb") as file:
                    file.write(base64.b64decode(content["data"]))
                parts.append(f"<image: {path}>")
            else:
                parts.append(content.get("text", f"<{content.get('type')}>"))
        text = json.dumps(reply["error"]) if "error" in reply else "\n".join(parts)
        print(f"== {tool} {json.dumps(args)} {'ERROR' if error else 'refused' if refused(parts) else 'ok'}")
        print(text[:limit], flush=True)

    bridge.close()
    if log:
        log.close()
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
