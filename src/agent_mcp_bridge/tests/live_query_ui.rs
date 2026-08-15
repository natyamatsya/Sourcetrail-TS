//! query_ui under the shape that used to time out: a search first (which fills
//! the tree and busies the GUI thread), then filter queries that match a lot.
//!
//! `query_ui` walks the whole accessibility tree on the GUI thread exactly as
//! `get_snapshot` does, then compiles a JSONPath, evaluates it and converts every
//! match — so it is never the faster of the two, and it used to be given a third
//! of the reply window. Both now share SNAPSHOT_TIMEOUT.
//!
//! Measured here in a settled app: ~350-1000 ms per query, filters included
//! (1417 matches for the window filter). The failures that prompted the fix were
//! >5 s on an app that had just loaded a project, and have not been reproducible
//! since — so treat this as a regression guard for the timings, not as a
//! reproduction of the original fault.
//!
//!   SOURCETRAIL_AGENT_INSTANCE=x cargo test --release --test live_query_ui \
//!       -- --ignored --nocapture
#[test]
#[ignore]
fn filter_queries_survive_the_post_search_state() {
    let inst = std::env::var("SOURCETRAIL_AGENT_INSTANCE").unwrap_or_default();
    let mut b = agent_mcp_bridge::Bridge::connect_instance(&inst).expect("connect");
    let _ = b.search("boundary");

    let queries = [
        "$.roots",
        "$.roots..[?(@.role=='button')]",
        "$..[?(@.role=='window')]",
        "$..[?(@.role=='list')]",
    ];
    let mut failures = 0;
    for round in 0..3 {
        for q in queries {
            let t = std::time::Instant::now();
            match b.query_ui(q) {
                Ok(v) => {
                    let n = v.get("roots").and_then(|r| r.as_array()).map(|a| a.len()).unwrap_or(0);
                    println!("round {round} {q:<32} OK   {n:>4} roots in {:?}", t.elapsed());
                }
                Err(e) => {
                    failures += 1;
                    println!("round {round} {q:<32} FAIL in {:?}: {}", t.elapsed(),
                             e.to_string().lines().next().unwrap());
                }
            }
        }
    }
    assert_eq!(failures, 0, "{failures} query_ui failures");
}
