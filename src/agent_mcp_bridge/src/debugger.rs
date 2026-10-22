//! The platform debugger behind an instance: crash dumps, live and post-mortem
//! debugging, driven by the bridge itself.
//!
//! |                | Windows                                  | macOS                                  |
//! |----------------|------------------------------------------|----------------------------------------|
//! | debugger       | cdb (Debugging Tools for Windows)        | lldb (Xcode's, or LLVM 21+)            |
//! | driven through | its console: commands on stdin, output from the console file up to a marker line | LLDB's MCP protocol server on a local port (JSON-RPC over TCP) |
//! | crash dump     | full minidump (`.dump /ma`), `.dmp`      | core of the modified memory, `.core`   |
//!
//! The debugger attaches after the app answered the handshake, so the process
//! id stays the app's (and on Windows the NT debug heap stays off: it is only
//! enabled for processes a debugger launches). A crash before the handshake
//! still surfaces as the instance's exit code.
//!
//! On a crash (an exception nobody handled) the debugger writes the dump beside
//! the instance's console file and holds the process stopped at the fault.
//! [`DebugSession::stop_status`] reports that stop, a breakpoint or a break-in;
//! [`DebugSession::command`] runs a command in the debugger's own language,
//! stopping a running process for it; [`open_dump`] opens a dump post-mortem.

use std::io::{BufRead, BufReader, Read, Seek, SeekFrom, Write};
use std::net::{SocketAddr, TcpStream};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Context, Result};
use serde_json::{json, Value};

/// How long attaching may take.
const ATTACH_TIMEOUT: Duration = Duration::from_secs(60);
/// How long opening a dump may take (symbol loading for a large app included).
const OPEN_DUMP_TIMEOUT: Duration = Duration::from_secs(300);
/// How long one debugger command may take (symbol loading included).
const COMMAND_TIMEOUT: Duration = Duration::from_secs(120);
/// How long the stopped-process check may take: it runs before tool calls.
const STATUS_TIMEOUT: Duration = Duration::from_secs(2);
/// How long a break-in may take to stop the process.
const BREAK_IN_TIMEOUT: Duration = Duration::from_secs(10);
/// Printed by the debugger once its setup ran.
const READY_MARKER: &str = "SOURCETRAIL-MCP-DEBUGGER-READY";

/// Exceptions whose second chance (nobody handled them) is a crash. `*` covers
/// every exception without an event name of its own.
pub const CDB_CRASH_EVENTS: &[&str] = &["av", "asrt", "dz", "eh", "gp", "ii", "ip", "isc", "sbo", "sov", "*"];

/// Microsoft's public symbol server, for cdb when `_NT_SYMBOL_PATH` is unset.
const MS_SYMBOL_SERVER: &str = "srv*https://msdl.microsoft.com/download/symbols";

/// lldb stop-hook that saves a core when the process stops on a crash: a
/// machine exception (EXC_BAD_ACCESS, ...) or a fatal signal (SIGABRT from
/// abort/qFatal, ...). Imported as a module, so its file name is fixed.
const LLDB_HOOK_MODULE: &str = "sourcetrail_mcp_lldb";
const LLDB_HOOK: &str = r#"import os

import lldb

CRASH_SIGNALS = {"SIGSEGV", "SIGBUS", "SIGILL", "SIGABRT", "SIGFPE", "SIGSYS"}


class DumpOnCrash:
    """Saves the modified memory to `core` on the first crash stop. Written
    under another name and renamed when complete, so `core` is never partial."""

    def __init__(self, target, extra_args, internal_dict):
        self.core = extra_args.GetValueForKey("core").GetStringValue(4096)
        self.written = False

    def handle_stop(self, exe_ctx, stream):
        if self.written or not crashed(exe_ctx.GetProcess()):
            return True
        self.written = True
        partial = self.core + ".partial"
        result = lldb.SBCommandReturnObject()
        exe_ctx.GetTarget().GetDebugger().GetCommandInterpreter().HandleCommand(
            'process save-core -s modified-memory "%s"' % partial, result)
        if result.Succeeded():
            os.replace(partial, self.core)
        stream.Print(result.GetOutput() or result.GetError() or "")
        return True  # stay stopped at the fault


def crashed(process):
    signals = process.GetUnixSignals()
    for thread in process:
        reason = thread.GetStopReason()
        if reason == lldb.eStopReasonException:
            return True
        if reason == lldb.eStopReasonSignal and \
                signals.GetSignalAsCString(thread.GetStopReasonDataAtIndex(0)) in CRASH_SIGNALS:
            return True
    return False
"#;

/// A debugger on one process (live) or one dump (post-mortem); dropping it ends
/// the debugger.
pub struct DebugSession {
    child: Child,
    driver: Driver,
    /// The debugged process; `None` for a dump.
    pid: Option<u32>,
    /// Where a crash dump goes (live), or the dump that is open (post-mortem).
    dump: PathBuf,
    /// The debugger's own console output.
    console: PathBuf,
    /// For a human debugger alongside the bridge: cdb's remote pipe
    /// (`cdb -remote`), lldb's MCP server.
    connect: Option<String>,
}

enum Driver {
    /// lldb, through its MCP protocol server on `port`.
    Lldb {
        port: u16,
        /// Held open: lldb quits on EOF.
        _stdin: ChildStdin,
    },
    /// cdb, through its console.
    Cdb(Mutex<CdbConsole>),
}

struct CdbConsole {
    stdin: ChildStdin,
    /// Console-file offset where the target last resumed: a stop prints after it.
    resumed_at: u64,
    /// Numbers the end-of-output markers.
    next_marker: u64,
}

impl DebugSession {
    /// "cdb" or "lldb".
    pub fn kind(&self) -> &'static str {
        match self.driver {
            Driver::Lldb { .. } => "lldb",
            Driver::Cdb(_) => "cdb",
        }
    }

    pub fn describe(&self) -> Value {
        let mut v = json!({
            "debugger": self.kind(),
            "debugger_pid": self.child.id(),
            "dump": self.dump.display().to_string(),
            "dump_written": std::fs::metadata(&self.dump).is_ok_and(|m| m.len() > 0),
            "debugger_console": self.console.display().to_string(),
        });
        if let Some(connect) = &self.connect {
            v["connect"] = json!(connect);
        }
        if self.pid.is_none() {
            v["post_mortem"] = json!(true);
        }
        v
    }

    pub fn dump_path(&self) -> &Path {
        &self.dump
    }

    /// The command that resumes the process.
    pub fn resume_command(&self) -> &'static str {
        match self.driver {
            Driver::Lldb { .. } => "process continue",
            Driver::Cdb(_) => "g",
        }
    }

    /// Why the process is stopped in the debugger (the crash, breakpoint or
    /// break-in, with the frame), or `None` while it runs or when the debugger
    /// is gone. A dump is always stopped.
    pub fn stop_status(&self) -> Option<String> {
        match &self.driver {
            Driver::Lldb { port, .. } => match lldb_mcp_command(*port, "process status", STATUS_TIMEOUT) {
                Ok(status) => stop_summary(&status),
                // lldb is slow to answer while it handles a stop, e.g. writing the
                // crash dump: the process is stopped meanwhile.
                Err(e) if e.to_string().contains("did not answer") => {
                    Some("stopped; the debugger is busy (e.g. writing the crash dump), retry shortly".into())
                }
                Err(_) => None,
            },
            Driver::Cdb(console) => {
                let state = console.lock().unwrap_or_else(|e| e.into_inner());
                cdb_stop_summary(&read_from(&self.console, state.resumed_at).ok()?)
            }
        }
    }

    /// The exit status of a process that exited under lldb, which keeps it from
    /// its parent (cdb ends with the process, so its parent sees the exit).
    pub fn exit_status(&self) -> Option<i32> {
        match &self.driver {
            Driver::Lldb { port, .. } => exited_status(&lldb_mcp_command(*port, "process status", STATUS_TIMEOUT).ok()?),
            Driver::Cdb(_) => None,
        }
    }

    /// Stop the process in the debugger and return the stop. lldb: `process
    /// interrupt`; cdb: DebugBreakProcess.
    pub fn break_in(&self) -> Result<String> {
        if let Some(stop) = self.stop_status() {
            return Ok(stop);
        }
        let pid = self.pid.ok_or_else(|| anyhow!("a dump has no process to stop"))?;
        match &self.driver {
            Driver::Lldb { port, .. } => {
                lldb_mcp_command(*port, "process interrupt", COMMAND_TIMEOUT)?;
            }
            Driver::Cdb(_) => debug_break_process(pid)?,
        }
        let deadline = Instant::now() + BREAK_IN_TIMEOUT;
        loop {
            if let Some(stop) = self.stop_status() {
                return Ok(stop);
            }
            if Instant::now() >= deadline {
                bail!("the process did not stop within {BREAK_IN_TIMEOUT:?} of the break-in");
            }
            std::thread::sleep(Duration::from_millis(100));
        }
    }

    /// Run a command in the debugger's own language and return its output. A
    /// running process is stopped for the command and resumed afterwards (unless
    /// the command resumed or ended it); a resume command on a running process
    /// does nothing.
    pub fn command(&self, command: &str) -> Result<String> {
        if self.pid.is_none() || self.stop_status().is_some() {
            return self.raw_command(command);
        }
        if self.is_resume(command) {
            return Ok("The process is running already.".into());
        }
        // lldb takes many commands while the process runs (a signal, a
        // breakpoint) and refuses the rest; only those need the stop. cdb reads
        // no command until the target stops.
        if let Driver::Lldb { .. } = self.driver {
            match self.raw_command(command) {
                Err(e) if lldb_needs_stop(&e.to_string()) => {}
                other => return other,
            }
        }
        self.break_in()?;
        let output = self.raw_command(command);
        if self.stop_status().is_some() {
            self.raw_command(self.resume_command())?;
        }
        output
    }

    fn is_resume(&self, command: &str) -> bool {
        match self.driver {
            Driver::Lldb { .. } => is_lldb_resume(command),
            Driver::Cdb(_) => is_cdb_resume(command),
        }
    }

    fn raw_command(&self, command: &str) -> Result<String> {
        match &self.driver {
            Driver::Lldb { port, .. } => lldb_mcp_command(*port, command, COMMAND_TIMEOUT),
            Driver::Cdb(console) => {
                let mut state = console.lock().unwrap_or_else(|e| e.into_inner());
                state.run(command, &self.console)
            }
        }
    }

    /// End the debugger, releasing the process it traces. Idempotent.
    ///
    /// Call this before killing and reaping the traced process: while the
    /// debugger holds a process at a fault, its parent cannot reap it (on
    /// Windows the wait blocks indefinitely, on macOS it returns ECHILD and
    /// leaves a zombie).
    pub fn end(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

impl Drop for DebugSession {
    fn drop(&mut self) {
        self.end();
    }
}

impl CdbConsole {
    /// Write `command` to cdb, followed by an `.echo` of a marker, and return the
    /// output up to the marker. A resume is written alone (a queued `.echo` would
    /// only run at the next stop) and returns at once.
    fn run(&mut self, command: &str, console: &Path) -> Result<String> {
        let start = std::fs::metadata(console).map(|m| m.len()).unwrap_or(0);
        if is_cdb_resume(command) {
            writeln!(self.stdin, "{}", command.trim()).context("write to cdb")?;
            self.stdin.flush().context("write to cdb")?;
            self.resumed_at = start;
            return Ok(String::new());
        }
        self.next_marker += 1;
        let marker = format!("SOURCETRAIL-MCP-END-{}", self.next_marker);
        write!(self.stdin, "{}\n.echo {marker}\n", command.trim()).context("write to cdb")?;
        self.stdin.flush().context("write to cdb")?;
        let deadline = Instant::now() + COMMAND_TIMEOUT;
        loop {
            if let Some(output) = cdb_output(&read_from(console, start)?, command, &marker) {
                return Ok(output);
            }
            if Instant::now() >= deadline {
                bail!("cdb did not finish the command within {COMMAND_TIMEOUT:?}; see {}", console.display());
            }
            std::thread::sleep(Duration::from_millis(50));
        }
    }
}

/// The file's contents from `offset` on.
fn read_from(path: &Path, offset: u64) -> Result<String> {
    let mut file = std::fs::File::open(path).with_context(|| format!("open {}", path.display()))?;
    file.seek(SeekFrom::Start(offset))?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes)?;
    Ok(String::from_utf8_lossy(&bytes).into_owned())
}

// --- cdb console parsing (pure, tested) ---------------------------------------

/// The length of one cdb prompt at the start of `s` (`0:000> `, `0:000:x86> `:
/// process:thread[:mode]), if it starts with one.
fn cdb_prompt_len(s: &str) -> Option<usize> {
    let digits = |s: &str| s.bytes().take_while(u8::is_ascii_digit).count();
    let mut at = digits(s);
    if at == 0 || s.as_bytes().get(at) != Some(&b':') {
        return None;
    }
    at += 1;
    let n = digits(&s[at..]);
    if n == 0 {
        return None;
    }
    at += n;
    if s.as_bytes().get(at) == Some(&b':') {
        let word = s[at + 1..].bytes().take_while(|b| b.is_ascii_alphanumeric() || *b == b'_').count();
        if word == 0 {
            return None;
        }
        at += 1 + word;
    }
    if s.as_bytes().get(at) != Some(&b'>') {
        return None;
    }
    at += 1;
    if s.as_bytes().get(at) == Some(&b' ') {
        at += 1;
    }
    Some(at)
}

/// `line` without the cdb prompts it starts with.
fn strip_cdb_prompts(line: &str) -> &str {
    let mut rest = line;
    while let Some(n) = cdb_prompt_len(rest) {
        rest = &rest[n..];
    }
    rest
}

/// Whether `line` is a bare cdb prompt.
fn is_cdb_prompt_only(line: &str) -> bool {
    let line = line.trim();
    cdb_prompt_len(line).is_some_and(|n| line[n..].trim().is_empty())
}

/// An event line, `(pid.tid): Access violation - code c0000005 (...)`: its text.
fn cdb_event(line: &str) -> Option<&str> {
    let rest = line.strip_prefix('(')?;
    let (ids, text) = rest.split_once("): ")?;
    let (pid, tid) = ids.split_once('.')?;
    let hex = |s: &str| !s.is_empty() && s.bytes().all(|b| b.is_ascii_hexdigit());
    (hex(pid) && hex(tid)).then_some(text)
}

/// `Breakpoint 3 hit`.
fn is_cdb_breakpoint(line: &str) -> bool {
    line.strip_prefix("Breakpoint ")
        .and_then(|r| r.strip_suffix(" hit"))
        .is_some_and(|n| !n.is_empty() && n.bytes().all(|b| b.is_ascii_digit()))
}

/// Whether a cdb command resumes the target (`g` and its variants gc/gh/gn/gu).
pub fn is_cdb_resume(command: &str) -> bool {
    let c = command.trim_start().to_ascii_lowercase();
    let word = c.split_whitespace().next().unwrap_or("");
    matches!(word, "g" | "gc" | "gh" | "gn" | "gu")
}

/// Whether an lldb command resumes the process.
pub fn is_lldb_resume(command: &str) -> bool {
    let words: Vec<&str> = command.split_whitespace().collect();
    matches!(words.as_slice(), ["c", ..] | ["continue", ..] | ["process", "continue", ..] | ["thread", "continue", ..])
}

/// The output of `command` in the console text written since it was sent, once
/// the marker line arrived: prompts, an echo of the command and the marker's
/// `.echo` removed.
pub fn cdb_output(text: &str, command: &str, marker: &str) -> Option<String> {
    let echo = format!(".echo {marker}");
    let mut lines: Vec<&str> = Vec::new();
    for line in text.lines() {
        let line = strip_cdb_prompts(line);
        let trimmed = line.trim();
        if trimmed == marker {
            if lines.first().is_some_and(|l| l.trim() == command.trim()) {
                lines.remove(0);
            }
            return Some(lines.join("\n").trim_end().to_string());
        }
        if trimmed != echo {
            lines.push(line);
        }
    }
    None
}

/// The stop in the console text written since the target last resumed: `None`
/// while it runs (cdb prompts only when the target is stopped), else the last
/// event (crash, break-in, breakpoint) with its symbol.
pub fn cdb_stop_summary(text: &str) -> Option<String> {
    let lines: Vec<&str> = text.lines().map(str::trim_end).filter(|l| !l.trim().is_empty()).collect();
    if !is_cdb_prompt_only(lines.last()?) {
        return None;
    }
    // A bare prompt before a resume was read is not a stop.
    if lines.iter().all(|l| is_cdb_prompt_only(l)) {
        return None;
    }
    let lines: Vec<&str> = lines.iter().map(|l| strip_cdb_prompts(l).trim()).collect();
    let Some(at) = lines.iter().rposition(|l| cdb_event(l).is_some() || is_cdb_breakpoint(l)) else {
        return Some("stopped at the cdb prompt".into());
    };
    let event = cdb_event(lines[at]).unwrap_or(lines[at]);
    // The line after an event names where it happened: `module!function+0x2a:`.
    match lines.get(at + 1).filter(|l| l.contains('!') && l.ends_with(':')) {
        Some(symbol) => Some(format!("{event}; {}", symbol.trim_end_matches(':'))),
        None => Some(event.to_string()),
    }
}

/// The cdb script run right after attaching: on any crash, write a full dump and
/// stay stopped at the fault; quit when the process exits; announce readiness.
/// Pure (tested).
///
/// It deliberately does NOT resume the target: the target stays at cdb's initial
/// break until [`attach_cdb`] resumes it through the console, which records the
/// resume offset first. Resumed from here, the target could run (and crash, or
/// exit) before the bridge took that offset, and a crash would read as a bare
/// "stopped at the cdb prompt" (its event lines before the offset).
pub fn cdb_script(dump: &Path) -> String {
    let action = format!(".dump /ma /o \\\"{}\\\"", cdb_path_literal(dump));
    let mut s = String::new();
    for event in CDB_CRASH_EVENTS {
        s.push_str(&format!("sxd -c2 \"{action}\" {event}\n"));
    }
    // A normal exit ends the debugger too (otherwise it would wait for input).
    s.push_str("sxe -c \"q\" epr\n");
    s.push_str(&format!(".echo {READY_MARKER}\n"));
    s
}

/// Forward slashes: Windows accepts them in file paths, and they need no
/// escaping inside cdb's quoted command strings.
fn cdb_path_literal(p: &Path) -> String {
    p.display().to_string().replace('\\', "/")
}

// --- lldb ---------------------------------------------------------------------

/// The stop from lldb's `process status` output: the status line and the stopped
/// thread's stop reason and frame, or `None` when not stopped. Pure (tested).
pub fn stop_summary(status: &str) -> Option<String> {
    let mut lines = status.lines();
    let first = lines.next()?.trim();
    if !(first.starts_with("Process ") && first.ends_with(" stopped")) {
        return None;
    }
    let mut summary = vec![first.to_string()];
    let mut rest = lines.skip_while(|l| !l.contains("stop reason"));
    if let Some(reason) = rest.next() {
        summary.push(reason.trim().trim_start_matches("* ").to_string());
        if let Some(frame) = rest.find(|l| l.trim_start().starts_with("frame #0")) {
            summary.push(frame.trim().to_string());
        }
    }
    Some(summary.join("; "))
}

/// Whether lldb refused a command only because the process runs. Pure (tested).
pub fn lldb_needs_stop(error: &str) -> bool {
    error.contains("requires a process which is currently stopped") || error.contains("Process is running.")
}

/// The status in lldb's `process status` of a process that exited
/// (`Process 77222 exited with status = 3 (0x00000003)`). Pure (tested).
pub fn exited_status(status: &str) -> Option<i32> {
    let rest = status.trim_start().strip_prefix("Process ")?;
    let (pid, rest) = rest.split_once(' ')?;
    if pid.is_empty() || !pid.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    let code = rest.strip_prefix("exited with status = ")?;
    let end = code.find(|c: char| c != '-' && !c.is_ascii_digit()).unwrap_or(code.len());
    code[..end].parse().ok()
}

/// The exit code of a debugged child, or `None` while it runs (a crashed one held
/// at the fault runs, too). A traced child cannot be waited for by its parent on
/// macOS (ECHILD): then the debugger reports the status, else the process is
/// gone without one (-1).
pub fn child_exit(child: &mut Child, session: Option<&DebugSession>) -> Option<i32> {
    match child.try_wait() {
        Ok(Some(status)) => Some(status.code().unwrap_or(-1)),
        Ok(None) => None,
        Err(_) => session.and_then(DebugSession::exit_status).or_else(|| (!process_exists(child.id())).then_some(-1)),
    }
}

/// Whether a process with this id exists (a zombie counts).
fn process_exists(pid: u32) -> bool {
    #[cfg(unix)]
    {
        extern "C" {
            fn kill(pid: i32, sig: i32) -> i32;
        }
        // SAFETY: signal 0 only checks that the process exists.
        let alive = unsafe { kill(pid as i32, 0) } == 0;
        alive || std::io::Error::last_os_error().raw_os_error() == Some(1) // EPERM: exists, not ours
    }
    #[cfg(not(unix))]
    {
        let _ = pid;
        true
    }
}

/// One command through an lldb MCP protocol server: newline-delimited JSON-RPC
/// over TCP; `initialize`, then the server's `command` tool.
fn lldb_mcp_command(port: u16, command: &str, timeout: Duration) -> Result<String> {
    let stream = TcpStream::connect_timeout(&SocketAddr::from(([127, 0, 0, 1], port)), Duration::from_secs(2))
        .with_context(|| format!("connect to lldb's MCP server on port {port}"))?;
    stream.set_read_timeout(Some(timeout))?;
    let mut writer = stream.try_clone()?;
    let mut reader = BufReader::new(stream);
    let mut send = |msg: Value| -> Result<()> { writeln!(writer, "{msg}").context("write to lldb's MCP server") };
    send(json!({ "jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": { "name": "sourcetrail-mcp", "version": env!("CARGO_PKG_VERSION") },
    }}))?;
    read_response(&mut reader, 1, timeout)?;
    send(json!({ "jsonrpc": "2.0", "method": "notifications/initialized" }))?;
    send(json!({ "jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {
        "name": "command", "arguments": { "command": command },
    }}))?;
    let result = read_response(&mut reader, 2, timeout)?;
    let text: String = result["content"]
        .as_array()
        .map(|blocks| blocks.iter().filter_map(|b| b["text"].as_str()).collect())
        .unwrap_or_default();
    if result["isError"].as_bool() == Some(true) {
        bail!("lldb: {}", text.trim());
    }
    Ok(text)
}

/// The result of request `id`, skipping notifications.
fn read_response(reader: &mut impl BufRead, id: u64, timeout: Duration) -> Result<Value> {
    let mut line = String::new();
    loop {
        line.clear();
        match reader.read_line(&mut line) {
            Ok(0) => bail!("lldb's MCP server closed the connection"),
            Ok(_) => {}
            Err(e) if matches!(e.kind(), std::io::ErrorKind::WouldBlock | std::io::ErrorKind::TimedOut) => {
                bail!("lldb did not answer within {timeout:?}")
            }
            Err(e) => return Err(e).context("read from lldb's MCP server"),
        }
        let Ok(msg) = serde_json::from_str::<Value>(&line) else { continue };
        if msg["id"].as_u64() != Some(id) {
            continue;
        }
        if let Some(error) = msg.get("error") {
            bail!("lldb's MCP server: {}", error["message"].as_str().unwrap_or("error"));
        }
        return Ok(msg["result"].clone());
    }
}

// --- lookup, attach, open -----------------------------------------------------

/// cdb.exe: `$CDB_PATH`, the Windows SDK's Debuggers, else PATH.
pub fn find_cdb() -> Option<PathBuf> {
    if let Some(p) = std::env::var_os("CDB_PATH").map(PathBuf::from).filter(|p| p.is_file()) {
        return Some(p);
    }
    for base in [std::env::var_os("ProgramFiles(x86)"), std::env::var_os("ProgramFiles")].into_iter().flatten() {
        let p = PathBuf::from(base).join("Windows Kits").join("10").join("Debuggers").join("x64").join("cdb.exe");
        if p.is_file() {
            return Some(p);
        }
    }
    which("cdb.exe")
}

/// Whether this platform has a debugger the bridge can attach.
pub fn available() -> bool {
    if cfg!(windows) {
        find_cdb().is_some()
    } else if cfg!(target_os = "macos") {
        which("lldb").is_some()
    } else {
        false
    }
}

fn which(name: &str) -> Option<PathBuf> {
    std::env::var_os("PATH").and_then(|paths| std::env::split_paths(&paths).map(|d| d.join(name)).find(|p| p.is_file()))
}

fn with_suffix(stem: &Path, suffix: &str) -> PathBuf {
    let mut s = stem.as_os_str().to_owned();
    s.push(suffix);
    PathBuf::from(s)
}

/// Attach the platform debugger to `pid`. `stem` is the instance's run path
/// without extension; the dump and the debugger's files go beside it. `id` names
/// cdb's remote pipe.
pub fn attach(pid: u32, id: &str, stem: &Path) -> Result<DebugSession> {
    if cfg!(windows) {
        attach_cdb(pid, id, stem)
    } else if cfg!(target_os = "macos") {
        attach_lldb(pid, stem)
    } else {
        bail!("no debugger integration on this platform (Windows: cdb, macOS: lldb)")
    }
}

/// Open a dump (`.dmp` on Windows, a core on macOS) in the platform debugger,
/// post-mortem. The debugger's console goes to `<stem>.<debugger>.log`.
pub fn open_dump(dump: &Path, stem: &Path) -> Result<DebugSession> {
    if !dump.is_file() {
        bail!("no dump at {}", dump.display());
    }
    if cfg!(windows) {
        let cdb = cdb_path()?;
        let console = with_suffix(stem, ".cdb.log");
        let mut cmd = Command::new(&cdb);
        cmd.arg("-z").arg(dump);
        with_symbol_path(&mut cmd);
        let (mut child, mut stdin) = spawn_with_console(&mut cmd, &console)?;
        writeln!(stdin, ".echo {READY_MARKER}").context("write to cdb")?;
        wait_ready(&mut child, &console, OPEN_DUMP_TIMEOUT)?;
        let resumed_at = 0; // the whole console: loading the dump names its exception
        Ok(DebugSession {
            child,
            driver: Driver::Cdb(Mutex::new(CdbConsole { stdin, resumed_at, next_marker: 0 })),
            pid: None,
            dump: dump.to_path_buf(),
            console,
            connect: None,
        })
    } else if cfg!(target_os = "macos") {
        let lldb = lldb_path()?;
        let port = free_port()?;
        let console = with_suffix(stem, ".lldb.log");
        let mut cmd = Command::new(&lldb);
        cmd.arg("-c").arg(dump);
        lldb_server_args(&mut cmd, port);
        cmd.arg("-o").arg(format!("script print('{READY_MARKER}')"));
        let (mut child, stdin) = spawn_with_console(&mut cmd, &console)?;
        wait_ready(&mut child, &console, OPEN_DUMP_TIMEOUT)?;
        Ok(DebugSession {
            child,
            driver: Driver::Lldb { port, _stdin: stdin },
            pid: None,
            dump: dump.to_path_buf(),
            console,
            connect: Some(format!("listen://localhost:{port}")),
        })
    } else {
        bail!("no debugger integration on this platform (Windows: cdb, macOS: lldb)")
    }
}

fn cdb_path() -> Result<PathBuf> {
    find_cdb().ok_or_else(|| anyhow!("cdb.exe not found: install the Debugging Tools for Windows (Windows SDK) or set CDB_PATH"))
}

fn lldb_path() -> Result<PathBuf> {
    which("lldb").ok_or_else(|| anyhow!("lldb not found on PATH (Xcode command line tools or LLVM 21+)"))
}

/// A free local port for LLDB's MCP protocol server.
fn free_port() -> Result<u16> {
    Ok(std::net::TcpListener::bind("127.0.0.1:0")?.local_addr()?.port())
}

/// Microsoft's symbol server for system modules, unless `_NT_SYMBOL_PATH` (which
/// cdb reads itself) says otherwise. The app's own PDBs are found through the
/// paths recorded in its binaries.
fn with_symbol_path(cmd: &mut Command) {
    if std::env::var_os("_NT_SYMBOL_PATH").is_none() {
        cmd.arg("-y").arg(MS_SYMBOL_SERVER);
    }
}

/// lldb's MCP protocol server on `port`, in asynchronous mode (otherwise a
/// `process continue` would block lldb until the next stop).
fn lldb_server_args(cmd: &mut Command, port: u16) {
    cmd.arg("-o").arg(format!("protocol-server start MCP listen://localhost:{port}"));
    cmd.arg("-o").arg("script lldb.debugger.SetAsync(True)");
}

/// Spawn with stdin piped (kept by the session) and stdout + stderr in `console`.
fn spawn_with_console(cmd: &mut Command, console: &Path) -> Result<(Child, ChildStdin)> {
    let out = std::fs::File::create(console).with_context(|| format!("create {}", console.display()))?;
    let mut child = cmd
        .stdin(Stdio::piped())
        .stdout(out.try_clone()?)
        .stderr(out)
        .spawn()
        .with_context(|| format!("spawn {}", cmd.get_program().to_string_lossy()))?;
    let stdin = child.stdin.take().context("debugger stdin")?;
    Ok((child, stdin))
}

/// Wait until the debugger printed the ready marker into its console file.
fn wait_ready(child: &mut Child, console: &Path, timeout: Duration) -> Result<()> {
    let deadline = Instant::now() + timeout;
    loop {
        // The marker first: the debugger may have finished its setup and then
        // ended with the target (cdb quits when the target exits), and one poll
        // can see both. That is a target that did not outlive the attach, not a
        // debugger that failed to start.
        if std::fs::read(console).is_ok_and(|b| String::from_utf8_lossy(&b).contains(READY_MARKER)) {
            return Ok(());
        }
        if let Some(status) = child.try_wait()? {
            bail!("debugger exited while starting ({status}); see {}", console.display());
        }
        if Instant::now() >= deadline {
            let _ = child.kill();
            bail!("debugger did not start within {timeout:?}; see {}", console.display());
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

fn attach_cdb(pid: u32, id: &str, stem: &Path) -> Result<DebugSession> {
    let cdb = cdb_path()?;
    let dump = with_suffix(stem, ".dmp");
    let script = with_suffix(stem, ".cdb.txt");
    let console = with_suffix(stem, ".cdb.log");
    std::fs::write(&script, cdb_script(&dump)).with_context(|| format!("write {}", script.display()))?;
    let pipe = format!("sourcetrail-{id}");
    let mut cmd = Command::new(&cdb);
    cmd.arg("-server").arg(format!("npipe:pipe={pipe}")).arg("-p").arg(pid.to_string()).arg("-cf").arg(&script);
    with_symbol_path(&mut cmd);
    let (mut child, stdin) = spawn_with_console(&mut cmd, &console)?;
    wait_ready(&mut child, &console, ATTACH_TIMEOUT)?;
    // The script leaves the target at cdb's initial break, so nothing it does can
    // reach the console before the offset below is taken.
    let resumed_at = std::fs::metadata(&console).map(|m| m.len()).unwrap_or(0);
    let session = DebugSession {
        child,
        driver: Driver::Cdb(Mutex::new(CdbConsole { stdin, resumed_at, next_marker: 0 })),
        pid: Some(pid),
        dump,
        console,
        connect: Some(format!("npipe:Pipe={pipe},Server=localhost")),
    };
    // Resume through the console: `run` records the resume offset before it
    // writes, so a crash right after this still prints after the offset and is
    // read as the crash it is (see `cdb_script`).
    session.raw_command(session.resume_command()).context("resume the target after attaching")?;
    Ok(session)
}

fn attach_lldb(pid: u32, stem: &Path) -> Result<DebugSession> {
    let lldb = lldb_path()?;
    let port = free_port()?;
    let dump = with_suffix(stem, ".core");
    let console = with_suffix(stem, ".lldb.log");
    let hook = stem.with_file_name(format!("{LLDB_HOOK_MODULE}.py"));
    std::fs::write(&hook, LLDB_HOOK).with_context(|| format!("write {}", hook.display()))?;
    let mut cmd = Command::new(&lldb);
    cmd.arg("-p").arg(pid.to_string());
    lldb_server_args(&mut cmd, port);
    cmd.arg("-o").arg(format!("command script import \"{}\"", hook.display()));
    cmd.arg("-o").arg(format!("target stop-hook add -P {LLDB_HOOK_MODULE}.DumpOnCrash -k core -v \"{}\"", dump.display()));
    cmd.arg("-o").arg("process continue");
    cmd.arg("-o").arg(format!("script print('{READY_MARKER}')"));
    let (mut child, stdin) = spawn_with_console(&mut cmd, &console)?;
    wait_ready(&mut child, &console, ATTACH_TIMEOUT)?;
    Ok(DebugSession {
        child,
        driver: Driver::Lldb { port, _stdin: stdin },
        pid: Some(pid),
        dump,
        console,
        connect: Some(format!("listen://localhost:{port}")),
    })
}

/// Stop a process under cdb: DebugBreakProcess (Windows).
fn debug_break_process(pid: u32) -> Result<()> {
    #[cfg(windows)]
    {
        use std::ffi::c_void;
        #[link(name = "kernel32")]
        extern "system" {
            fn OpenProcess(desired_access: u32, inherit_handle: i32, process_id: u32) -> *mut c_void;
            fn DebugBreakProcess(process: *mut c_void) -> i32;
            fn CloseHandle(handle: *mut c_void) -> i32;
        }
        const PROCESS_ALL_ACCESS: u32 = 0x001F_0FFF;
        // SAFETY: plain Win32 calls on a handle opened and closed here.
        unsafe {
            let handle = OpenProcess(PROCESS_ALL_ACCESS, 0, pid);
            if handle.is_null() {
                bail!("cannot open process {pid}: {}", std::io::Error::last_os_error());
            }
            let ok = DebugBreakProcess(handle);
            let error = std::io::Error::last_os_error();
            CloseHandle(handle);
            if ok == 0 {
                bail!("DebugBreakProcess({pid}) failed: {error}");
            }
        }
        Ok(())
    }
    #[cfg(not(windows))]
    {
        let _ = pid;
        bail!("DebugBreakProcess is Windows-only")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn script_dumps_on_every_crash_event_and_stays_stopped() {
        let s = cdb_script(Path::new(r"C:\Users\A B\logs\x.dmp"));
        for event in CDB_CRASH_EVENTS {
            assert!(
                s.contains(&format!("sxd -c2 \".dump /ma /o \\\"C:/Users/A B/logs/x.dmp\\\"\" {event}\n")),
                "missing {event}:\n{s}"
            );
        }
        assert!(!s.contains(";q"), "a crash must not end the process:\n{s}");
        assert!(s.contains("sxe -c \"q\" epr\n"));
        // The script must end at the ready marker: resuming here would let the
        // target run before the bridge recorded the resume offset.
        assert!(s.ends_with(&format!(".echo {READY_MARKER}\n")), "script must not resume:\n{s}");
        assert!(!s.lines().any(is_cdb_resume), "script must not resume:\n{s}");
    }

    #[test]
    fn cdb_prompts_are_recognized() {
        assert_eq!(cdb_prompt_len("0:000> k"), Some(7));
        assert_eq!(cdb_prompt_len("0:000:x86> k"), Some(11));
        assert_eq!(cdb_prompt_len("12:034>"), Some(7));
        assert_eq!(cdb_prompt_len("0:> k"), None);
        assert_eq!(cdb_prompt_len("ntdll!DbgBreakPoint:"), None);
        assert_eq!(strip_cdb_prompts("0:000> 0:000> k 2"), "k 2");
        assert!(is_cdb_prompt_only("0:007> "));
        assert!(!is_cdb_prompt_only("0:007> k"));
    }

    #[test]
    fn cdb_output_strips_prompts_echoes_and_the_marker() {
        let m = "SOURCETRAIL-MCP-END-3";
        let text = format!(
            "0:000> k 2\n # Child-SP          RetAddr           Call Site\n00 0000005d`1a2ff6a8 00007ff7`4c1b1a3c crashme!main+0x5c\n0:000> {m}\n0:000> "
        );
        assert_eq!(
            cdb_output(&text, "k 2", m).as_deref(),
            Some(" # Child-SP          RetAddr           Call Site\n00 0000005d`1a2ff6a8 00007ff7`4c1b1a3c crashme!main+0x5c")
        );
        // A console that echoes the input.
        let echoed = format!("0:000> k 2\nframes\n0:000> .echo {m}\n{m}\n");
        assert_eq!(cdb_output(&echoed, "k 2", m).as_deref(), Some("frames"));
        // Not finished yet.
        assert_eq!(cdb_output("0:000> k 2\nframes\n", "k 2", m), None);
    }

    #[test]
    fn cdb_stop_summary_reads_crashes_breaks_and_running() {
        let crash = "\
(3b24.1f40): Access violation - code c0000005 (!!! second chance !!!)
crashme!crashme::main+0x5c:
00007ff7`4c1b1a3c c70001000000    mov     dword ptr [rax],1 ds:00000000`00000000=????????
Creating C:\\logs\\x.dmp - mini user dump
Dump successfully written
0:000> ";
        assert_eq!(
            cdb_stop_summary(crash).as_deref(),
            Some("Access violation - code c0000005 (!!! second chance !!!); crashme!crashme::main+0x5c")
        );
        let break_in = "\
(3b24.2a10): Break instruction exception - code 80000003 (first chance)
ntdll!DbgBreakPoint:
00007ffc`8d8d0b70 cc              int     3
0:007> k 1\n # Call Site\n00 ntdll!DbgBreakPoint\n0:007> ";
        assert_eq!(
            cdb_stop_summary(break_in).as_deref(),
            Some("Break instruction exception - code 80000003 (first chance); ntdll!DbgBreakPoint")
        );
        assert_eq!(
            cdb_stop_summary("Breakpoint 0 hit\ncrashme!crashme::main:\n00007ff7`4c1b1a00 4883ec28 sub rsp,28h\n0:000> ")
                .as_deref(),
            Some("Breakpoint 0 hit; crashme!crashme::main")
        );
        // Running: nothing since the resume, or output without a prompt yet.
        assert_eq!(cdb_stop_summary(""), None);
        assert_eq!(cdb_stop_summary("0:000> "), None);
        assert_eq!(cdb_stop_summary("ModLoad: 00007ffc`8a000000 C:\\x.dll\n"), None);
        // Stopped without a recognizable event.
        assert_eq!(cdb_stop_summary("something\n0:000> ").as_deref(), Some("stopped at the cdb prompt"));
    }

    #[test]
    fn resume_commands_are_recognized() {
        for c in ["g", " g ", "G", "gh", "gn", "gu", "gc", "g 00007ff7`4c1b1a00"] {
            assert!(is_cdb_resume(c), "{c}");
        }
        for c in ["k", "~*k", "bp crashme!main", ".dump /ma x", "dx this", "gflags"] {
            assert!(!is_cdb_resume(c), "{c}");
        }
        for c in ["c", "continue", "process continue", "thread continue 2"] {
            assert!(is_lldb_resume(c), "{c}");
        }
        for c in ["bt", "bt all", "command script import x", "process status", "frame variable"] {
            assert!(!is_lldb_resume(c), "{c}");
        }
    }

    #[test]
    fn stop_summary_reports_a_stop_and_ignores_a_running_process() {
        let crashed = "Process 4711 stopped\n\
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0x0)\n\
    frame #0: 0x0000000100003f10 crashme`crashme::main at crashme.rs:14:40\n\
   11  \t    let delay: u64 = 3000;\n";
        assert_eq!(
            stop_summary(crashed).as_deref(),
            Some("Process 4711 stopped; thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0x0); \
frame #0: 0x0000000100003f10 crashme`crashme::main at crashme.rs:14:40")
        );
        assert_eq!(stop_summary("Process 4711 running\n"), None);
        assert_eq!(stop_summary("error: Command requires a current process.\n"), None);
        assert_eq!(stop_summary(""), None);
    }

    #[test]
    fn lldb_needs_stop_knows_both_refusals_of_a_running_process() {
        assert!(lldb_needs_stop("lldb: error: Process is running.  Use 'process interrupt' to pause execution."));
        assert!(lldb_needs_stop("lldb: error: Command requires a process which is currently stopped."));
        assert!(!lldb_needs_stop("lldb: error: 'bogus' is not a valid command."));
        assert!(!lldb_needs_stop("lldb did not answer within 30s"));
    }

    #[test]
    fn exited_status_reads_the_exit_code() {
        assert_eq!(exited_status("Process 77222 exited with status = 3 (0x00000003)\n"), Some(3));
        assert_eq!(exited_status("Process 1 exited with status = -1 (0xffffffff) debugserver died\n"), Some(-1));
        assert_eq!(exited_status("Process 77222 stopped\n"), None);
        assert_eq!(exited_status("Process 77222 running\n"), None);
    }

    #[test]
    fn suffixes_keep_the_stem() {
        assert_eq!(with_suffix(Path::new("logs/a-1"), ".dmp"), PathBuf::from("logs/a-1.dmp"));
    }
}
