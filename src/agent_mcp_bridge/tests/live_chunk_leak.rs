//! Does a receiver that disconnects with an unread large message leak its chunk?
//!
//! `recycle_storage` frees a chunk when the last reader clears its bit from the
//! chunk's `conns` mask. A receiver that goes away without reading never clears
//! its bit, so the chunk is pinned — and the chunk shm outlives the process, so
//! the leak is permanent for that chunk size. There are 32 slots; leak them all
//! and every later large message falls back to 64-byte inline fragments.
//!
//! This is the shape x4b was in when 60 of 60 large replies came back corrupt:
//! the damage had been done by *earlier, already-closed* connections.
//!
//! Needs a running app with a project loaded and a broad search performed.

use std::time::{Duration, Instant};

use agent_mcp_bridge::Bridge;

fn instance() -> String {
    std::env::var("SOURCETRAIL_AGENT_INSTANCE").unwrap_or_default()
}

#[test]
#[ignore = "needs a running app with a project loaded and a search performed"]
fn connections_that_abandon_large_replies_do_not_poison_the_next_one() {
    // Baseline on a fresh connection, and how long a healthy large reply takes.
    let baseline = {
        let mut b = Bridge::connect_instance(&instance()).expect("connect");
        let started = Instant::now();
        b.get_ui_state().expect("baseline get_ui_state");
        started.elapsed()
    };
    println!("baseline large reply: {baseline:?}");

    // 48 short-lived connections, each abandoning a large reply, each then
    // dropped. 48 > the 32-slot pool, so a per-disconnect leak exhausts it.
    // One *shape* of reply throughout: chunk storage is pooled per chunk size
    // (the shm name embeds it), so alternating queries spreads the leaks over
    // several 32-slot pools and never exhausts any of them. UiState replies for
    // an unchanging app state land in the same size bucket every time.
    for _ in 0..48 {
        let mut b = Bridge::connect_instance(&instance()).expect("connect");
        let _ = b.get_ui_state_with_timeout(Duration::from_millis(1));
        drop(b);
    }

    // A brand-new connection must be as healthy as the first one was.
    let mut b = Bridge::connect_instance(&instance()).expect("connect");
    let mut failures = 0;
    let mut slowest = Duration::ZERO;
    for i in 0..10 {
        let started = Instant::now();
        match b.get_ui_state() {
            Ok(_) => slowest = slowest.max(started.elapsed()),
            Err(e) => {
                failures += 1;
                if failures <= 2 {
                    println!("iter {i} failed: {e}");
                }
            }
        }
    }
    let dropped = b.status()["dropped_frames"].as_u64().unwrap_or(0);
    println!("RESULT: {failures}/10 failures, dropped_frames={dropped}, slowest={slowest:?} (baseline {baseline:?})");

    assert_eq!(failures, 0, "a fresh connection inherited a broken channel");
    assert_eq!(dropped, 0, "frames arrived unparseable");
}
