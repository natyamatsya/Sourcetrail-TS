//! Test program for the debugger integration (tests/debugger.rs): waits, then
//! crashes (or exits) in the requested way. `crashme <av|abort|exit> <delay ms>`.
//!
//! abort goes through Rust's abort, which on Windows is __fastfail: the same
//! fail-fast path (0xC0000409) an application's qFatal/abort() takes.

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mode = args.get(1).map(String::as_str).unwrap_or("av");
    let delay: u64 = args.get(2).and_then(|d| d.parse().ok()).unwrap_or(3000);
    std::thread::sleep(std::time::Duration::from_millis(delay));
    match mode {
        // SAFETY: none: crashing is the point.
        "av" => unsafe { std::ptr::null_mut::<u32>().write_volatile(1) },
        "abort" => std::process::abort(),
        _ => std::process::exit(3),
    }
}
