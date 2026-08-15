//! Reliability under abandoned replies — the failure the other harnesses missed.
//!
//! A request that times out leaves its reply unread. For a reply large enough to
//! use chunk storage that pins a chunk, and there are only 32 slots per chunk
//! size. Exhaust them and the sender stops using chunks altogether, falling back
//! to 64-byte inline fragments: a 225 KB reply becomes ~3,500 fragments through a
//! 256-slot ring, which is slow enough to time out the *next* request too. That
//! is the cascade that ends in unparseable frames.
//!
//! What this actually measures, from the runs that produced it: abandoning 48
//! replies does not corrupt anything — `dropped_frames` stays 0 — but it costs
//! roughly 25-50x in latency. A large reply that answers in ~70-170 ms fresh
//! takes 2-4 s afterwards, which is most of the 5 s window; on an instance that
//! had already been abused by live_chunk_leak, 3 of 20 recovery requests tipped
//! over into timeouts. Latency, not corruption, is the reliable signal here, so
//! it is printed rather than asserted (a hard bound would be flaky on a shared
//! machine) — read the RESULT line, do not just look for a green tick.
//!
//! Needs a running app with a project loaded and a broad search performed, so
//! UiState is large enough to take the chunk path.
//!
//!   SOURCETRAIL_AGENT_INSTANCE=x cargo test --release \
//!       --test live_abandoned_replies -- --ignored --nocapture

use std::time::{Duration, Instant};

#[test]
#[ignore = "needs a running app with a project loaded and a search performed"]
fn a_run_of_abandoned_replies_leaves_the_connection_usable() {
    let instance = std::env::var("SOURCETRAIL_AGENT_INSTANCE").unwrap_or_default();
    let mut bridge = agent_mcp_bridge::Bridge::connect_instance(&instance).expect("connect");

    // Baseline: the connection works before we mistreat it.
    bridge.get_ui_state().expect("baseline get_ui_state");

    // Abandon well past the 32-slot pool. Errors are the point, not a failure.
    //
    // Searches, not get_ui_state: the state channel carries one reply per
    // request, so an abandoned one is consumed by the very next request and
    // never accumulates. Events arrive several per request (the ack, the large
    // SearchCompleted, status changes), so abandoning those outruns whatever
    // later requests consume — which is what actually drains the chunk pool.
    for i in 0..48 {
        let _ = bridge.search_with_timeout(
            if i % 2 == 0 { "Syntax" } else { "Index" },
            Duration::from_millis(1),
        );
    }
    let after_abandon = bridge.status()["dropped_frames"].as_u64().unwrap_or(0);
    println!("dropped_frames after 48 abandoned replies: {after_abandon}");

    // Recovery: the connection must still answer, and promptly.
    let mut failures = 0;
    let mut slowest = Duration::ZERO;
    for i in 0..20 {
        let started = Instant::now();
        match bridge.get_ui_state() {
            Ok(_) => slowest = slowest.max(started.elapsed()),
            Err(e) => {
                failures += 1;
                if failures <= 2 {
                    println!("recovery iter {i} failed: {e}");
                }
            }
        }
    }
    let dropped = bridge.status()["dropped_frames"].as_u64().unwrap_or(0);
    println!("RESULT: {failures}/20 recovery failures, dropped_frames={dropped}, slowest={slowest:?}");

    assert_eq!(failures, 0, "connection did not recover from abandoned replies");
    assert_eq!(dropped, 0, "frames arrived unparseable (dropped={dropped})");
}
