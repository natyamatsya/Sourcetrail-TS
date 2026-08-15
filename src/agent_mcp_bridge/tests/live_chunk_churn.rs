//! Discriminating test for the chunk-pool lock. Needs a running app with a
//! project loaded and a broad search performed (so UiState is large enough to go
//! through chunk storage).
//!
//! A fresh connection barely touches the chunk pool, so it passes either way.
//! The intent was churn — mixed traffic allocating and freeing chunk ids while
//! the peer does the same — as a way to tell a correct chunk-pool lock from a
//! broken one.
//!
//! It does NOT discriminate, and that is worth recording rather than deleting:
//! run against a deliberately reverted (os_unfair_lock) chunk lock it still
//! reports 0/40, as it does with the lock corrected and with the bridge's stale
//! drains disabled. Whatever state produced the original 60/60 corruption is
//! not what this reproduces. Do not read a pass here as evidence the pool is
//! sound.
#[test]
#[ignore = "needs a running app with a project loaded and a search performed"]
fn chunk_pool_survives_churn() {
    let instance = std::env::var("SOURCETRAIL_AGENT_INSTANCE").unwrap_or_default();
    let mut bridge = agent_mcp_bridge::Bridge::connect_instance(&instance).expect("connect");

    // Churn: mixed large replies across three channels, ignoring outcomes.
    for i in 0..12 {
        let _ = bridge.get_ui_state();
        let _ = bridge.search(if i % 2 == 0 { "Syntax" } else { "Index" });
        let _ = bridge.query_ui("$.roots..[?(@.role=='button')]");
    }
    let after_churn = bridge.status()["dropped_frames"].as_u64().unwrap_or(0);
    println!("dropped_frames after churn: {after_churn}");

    // Measure: large replies on the churned pool.
    let mut failures = 0;
    for i in 0..40 {
        if let Err(e) = bridge.get_ui_state() {
            failures += 1;
            if failures <= 2 {
                println!("iter {i} failed: {e}");
            }
        }
    }
    let dropped = bridge.status()["dropped_frames"].as_u64().unwrap_or(0);
    println!("RESULT: {failures}/40 failures, dropped_frames={dropped} (churn={after_churn})");
    assert_eq!(failures, 0, "large replies corrupted after chunk-pool churn");
    assert_eq!(dropped, 0, "frames dropped as unparseable");
}
