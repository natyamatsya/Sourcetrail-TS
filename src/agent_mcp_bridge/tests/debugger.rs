//! Integration test: the debugger the bridge attaches (cdb on Windows, lldb on
//! macOS): a crash writes a dump and holds the process at the fault, commands run
//! whether the process runs or not, a dump opens post-mortem. Skipped (with a
//! note) when the debugger is not installed. Uses the `crashme` example, built on
//! demand.
//!
//!   cargo test --test debugger -- --nocapture

#![cfg(any(windows, target_os = "macos"))]

use std::path::{Path, PathBuf};
use std::process::{Child, Command};
use std::time::{Duration, Instant};

use agent_mcp_bridge::debugger::{self, DebugSession};

/// The platform debugger's language for what the tests ask.
struct Lang {
    backtrace: &'static str,
    resume: &'static str,
    /// In the stop of an access violation, and of an abort.
    access_violation: &'static str,
    abort: &'static str,
}

const LANG: Lang = if cfg!(windows) {
    Lang { backtrace: "k", resume: "g", access_violation: "Access violation", abort: "c0000409" }
} else {
    Lang { backtrace: "bt", resume: "process continue", access_violation: "EXC_BAD_ACCESS", abort: "SIGABRT" }
};

fn crashme() -> PathBuf {
    let status = Command::new(env!("CARGO"))
        .args(["build", "--quiet", "--example", "crashme"])
        .current_dir(env!("CARGO_MANIFEST_DIR"))
        .status()
        .expect("cargo build --example crashme");
    assert!(status.success());
    // The example builds in the dev profile, whatever profile runs the tests.
    let exe = if cfg!(windows) { "crashme.exe" } else { "crashme" };
    Path::new(env!("CARGO_MANIFEST_DIR")).join("target").join("debug").join("examples").join(exe)
}

struct Run {
    child: Child,
    session: DebugSession,
    dir: PathBuf,
}

impl Drop for Run {
    fn drop(&mut self) {
        // End the debugger before reaping the app, the same order as
        // AppInstance::drop: a process the debugger holds cannot be waited for
        // until the debugger lets go.
        self.session.end();
        let _ = self.child.kill();
        let deadline = Instant::now() + Duration::from_secs(5);
        while !matches!(self.child.try_wait(), Ok(Some(_))) && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(20));
        }
        let _ = std::fs::remove_dir_all(&self.dir);
    }
}

/// Start `crashme <mode> <delay_ms>` and attach the debugger while it sleeps;
/// `None` when no debugger is installed.
fn attach(mode: &str, delay_ms: u32, tag: &str) -> Option<Run> {
    if !debugger::available() {
        eprintln!("no debugger installed (cdb / lldb): skipping the debugger integration test");
        return None;
    }
    let dir = std::env::temp_dir().join(format!("sourcetrail-mcp-dbg-{}-{tag}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let child = Command::new(crashme()).args([mode, &delay_ms.to_string()]).spawn().unwrap();
    let session = debugger::attach(child.id(), &format!("test-{tag}-{}", std::process::id()), &dir.join("instance"))
        .expect("attach the debugger");
    Some(Run { child, session, dir })
}

fn wait_until<T>(what: &str, mut f: impl FnMut() -> Option<T>) -> T {
    let deadline = Instant::now() + Duration::from_secs(60);
    loop {
        if let Some(v) = f() {
            return v;
        }
        assert!(Instant::now() < deadline, "timed out waiting for {what}");
        std::thread::sleep(Duration::from_millis(100));
    }
}

fn dump_written(session: &DebugSession) -> bool {
    std::fs::metadata(session.dump_path()).is_ok_and(|m| m.len() > 0)
}

#[test]
fn a_crash_writes_a_dump_and_holds_the_process_at_the_fault() {
    let Some(mut run) = attach("av", 3000, "av") else { return };
    assert_eq!(run.session.stop_status(), None, "stopped before the crash");
    let stop = wait_until("the crash", || run.session.stop_status());
    assert!(stop.contains(LANG.access_violation), "stop: {stop}");
    wait_until("the dump", || dump_written(&run.session).then_some(()));
    assert_eq!(debugger::child_exit(&mut run.child, Some(&run.session)), None, "the crashed process must be held");
    let bt = run.session.command(LANG.backtrace).expect("backtrace");
    assert!(bt.contains("crashme"), "backtrace: {bt}");

    // The dump opens post-mortem, with the same stop.
    let dump = run.session.dump_path().to_path_buf();
    let post = debugger::open_dump(&dump, &run.dir.join("post-mortem")).expect("open the dump");
    let bt = post.command(LANG.backtrace).expect("backtrace from the dump");
    assert!(bt.contains("crashme"), "backtrace from the dump: {bt}");
    assert!(post.stop_status().is_some(), "a dump is stopped");
}

#[test]
fn an_abort_is_a_crash_too() {
    let Some(run) = attach("abort", 3000, "abort") else { return };
    let stop = wait_until("the abort", || run.session.stop_status());
    assert!(stop.contains(LANG.abort), "stop: {stop}");
    wait_until("the dump", || dump_written(&run.session).then_some(()));
}

#[test]
fn a_normal_exit_writes_no_dump() {
    let Some(mut run) = attach("exit", 2000, "exit") else { return };
    let code = wait_until("the exit", || debugger::child_exit(&mut run.child, Some(&run.session)));
    assert_eq!(code, 3);
    assert!(!dump_written(&run.session), "a normal exit must not produce a dump");
}

#[test]
fn commands_run_while_the_process_runs_and_leave_it_running() {
    let Some(mut run) = attach("exit", 30_000, "running") else { return };
    assert_eq!(run.session.stop_status(), None);
    let bt = run.session.command(LANG.backtrace).expect("backtrace while running");
    assert!(!bt.trim().is_empty(), "empty backtrace");
    assert_eq!(run.session.stop_status(), None, "still stopped after the command");
    let resume = run.session.command(LANG.resume).expect("resume while running");
    assert!(resume.contains("running already"), "resume: {resume}");
    assert_eq!(debugger::child_exit(&mut run.child, Some(&run.session)), None);
}

#[test]
fn break_in_stops_and_resume_continues() {
    let Some(run) = attach("exit", 30_000, "break") else { return };
    let stop = run.session.break_in().expect("break in");
    assert_eq!(run.session.stop_status().as_deref(), Some(stop.as_str()));
    run.session.command(LANG.resume).expect("resume");
    wait_until("the resume", || run.session.stop_status().is_none().then_some(()));
}
