//! Phase 4 de-risking probe: does turso_core 0.7.0-pre.18 actually allow
//! *concurrent* write transactions from multiple connections to one MVCC
//! database? SQLite cannot (single-writer); the whole concurrent-writer design
//! rests on Turso being able to.
//!
//! Setup: one `Database` with `PRAGMA journal_mode = 'mvcc'`, N writer threads
//! each with its own connection, each doing `BEGIN CONCURRENT` + a batch of
//! inserts into a *disjoint* id range + `COMMIT`, all overlapping in time.
//! Success = every writer commits and the row count is exactly N * batch.
//!
//!   cargo run --bin mvcc_probe -- [writers] [rows_per_writer]

use std::sync::Arc;
use std::thread;

use turso_core::{Connection, Database, StepResult, PlatformIO, IO};

fn exec(conn: &Arc<Connection>, io: &Arc<dyn IO>, sql: &str) -> Result<(), String> {
    // Drive one statement to completion on this thread, pumping shared IO.
    let mut stmt = conn.prepare(sql).map_err(|e| format!("prepare `{sql}`: {e}"))?;
    loop {
        match stmt.step().map_err(|e| format!("step `{sql}`: {e}"))? {
            StepResult::Row | StepResult::Done => return Ok(()),
            StepResult::IO | StepResult::Yield | StepResult::Busy => {
                io.step().map_err(|e| format!("io.step `{sql}`: {e}"))?;
            }
            StepResult::Interrupt => return Err(format!("interrupted on `{sql}`")),
        }
    }
}

fn scalar(conn: &Arc<Connection>, io: &Arc<dyn IO>, sql: &str) -> Result<i64, String> {
    let mut stmt = conn.prepare(sql).map_err(|e| e.to_string())?;
    loop {
        match stmt.step().map_err(|e| e.to_string())? {
            StepResult::Row => {
                let v = stmt.row().and_then(|r| r.get_value(0).as_int()).unwrap_or(-1);
                return Ok(v);
            }
            StepResult::Done => return Ok(-1),
            StepResult::IO | StepResult::Yield | StepResult::Busy => {
                io.step().map_err(|e| e.to_string())?;
            }
            StepResult::Interrupt => return Err("interrupted".into()),
        }
    }
}

fn open_ro(path: &str) -> Result<(Arc<dyn IO>, Arc<Connection>), String> {
    let io: Arc<dyn IO> = Arc::new(PlatformIO::new().map_err(|e| e.to_string())?);
    let db = Database::open_file(io.clone(), path).map_err(|e| e.to_string())?;
    let conn = db.connect().map_err(|e| e.to_string())?;
    Ok((io, conn))
}

// Run a `SELECT a, b ...` and collect (a, b) integer pairs.
fn pair_rows(conn: &Arc<Connection>, io: &Arc<dyn IO>, sql: &str) -> Result<Vec<(i64, i64)>, String> {
    let mut stmt = conn.prepare(sql).map_err(|e| e.to_string())?;
    let mut out = Vec::new();
    loop {
        match stmt.step().map_err(|e| e.to_string())? {
            StepResult::Row => {
                let r = stmt.row().ok_or("row")?;
                let a = r.get_value(0).as_int().unwrap_or(0);
                let b = r.get_value(1).as_int().unwrap_or(0);
                out.push((a, b));
            }
            StepResult::Done => return Ok(out),
            StepResult::IO | StepResult::Yield | StepResult::Busy => {
                io.step().map_err(|e| e.to_string())?
            }
            StepResult::Interrupt => return Err("interrupt".into()),
        }
    }
}

fn count_of(conn: &Arc<Connection>, io: &Arc<dyn IO>, table: &str) -> Option<i64> {
    pair_rows(conn, io, &format!("SELECT 0, COUNT(*) FROM {table}"))
        .ok()
        .and_then(|v| v.first().map(|x| x.1))
}

fn compare_dbs(a: &str, b: &str) -> i32 {
    let (ioa, ca) = match open_ro(a) {
        Ok(x) => x,
        Err(e) => {
            println!("open A ({a}): {e}");
            return 2;
        }
    };
    let (iob, cb) = match open_ro(b) {
        Ok(x) => x,
        Err(e) => {
            println!("open B ({b}): {e}");
            return 2;
        }
    };
    println!("A = {a}\nB = {b}\n");

    let mut diffs = 0;
    let tables = [
        "element", "node", "edge", "symbol", "file", "filecontent", "local_symbol",
        "source_location", "occurrence", "component_access", "element_component", "error",
    ];
    println!("{:<20} {:>12} {:>12}", "table (count)", "A", "B");
    for t in tables {
        match (count_of(&ca, &ioa, t), count_of(&cb, &iob, t)) {
            (Some(x), Some(y)) => {
                let flag = if x != y {
                    diffs += 1;
                    "  <-- DIFF"
                } else {
                    ""
                };
                println!("{t:<20} {x:>12} {y:>12}{flag}");
            }
            _ => println!("{t:<20} {:>12} {:>12}  (missing)", "?", "?"),
        }
    }

    let hists = [
        ("node", "type"),
        ("edge", "type"),
        ("symbol", "definition_kind"),
        ("source_location", "type"),
        ("component_access", "type"),
        ("element_component", "type"),
        ("file", "line_count"),
    ];
    println!("\nvalue histograms (value -> count):");
    for (t, c) in hists {
        let sql = format!("SELECT {c}, COUNT(*) FROM {t} GROUP BY {c} ORDER BY {c}");
        let ha = pair_rows(&ca, &ioa, &sql).unwrap_or_default();
        let hb = pair_rows(&cb, &iob, &sql).unwrap_or_default();
        if ha == hb {
            println!("  {t}.{c}: IDENTICAL ({} buckets)", ha.len());
        } else {
            diffs += 1;
            println!("  {t}.{c}: DIFFERS\n      A={ha:?}\n      B={hb:?}");
        }
    }

    println!(
        "\n{}",
        if diffs == 0 {
            "RESULT: databases match (counts + histograms)."
        } else {
            "RESULT: divergences present."
        }
    );
    if diffs == 0 {
        0
    } else {
        1
    }
}

fn main() {
    // `mvcc_probe verify <path> <table>` — open an existing db and count a table
    // (durability check for a file written+checkpointed by another process).
    let raw: Vec<String> = std::env::args().skip(1).collect();
    if raw.first().map(|s| s.as_str()) == Some("verify") {
        let path = raw.get(1).cloned().unwrap_or_default();
        let table = raw.get(2).cloned().unwrap_or_else(|| "node".to_string());
        let io: Arc<dyn IO> = Arc::new(PlatformIO::new().unwrap());
        let db = Database::open_file(io.clone(), &path).unwrap();
        let conn = db.connect().unwrap();
        let n = scalar(&conn, &io, &format!("SELECT COUNT(*) FROM {table}")).unwrap_or(-1);
        println!("verify {path}: {table} count = {n}");
        std::process::exit(if n > 0 { 0 } else { 1 });
    }

    // `mvcc_probe compare <db_a> <db_b>` — compare two graph databases (either
    // turso-MVCC or sqlite; turso_core opens both) by raw row counts and by
    // value-distribution histograms of the columns where value-divergences would
    // show (node/edge/source_location type, symbol definition_kind, ...). This
    // sidesteps the fact that absolute ids permute across runs.
    if raw.first().map(|s| s.as_str()) == Some("compare") {
        let a = raw.get(1).cloned().unwrap_or_default();
        let b = raw.get(2).cloned().unwrap_or_default();
        std::process::exit(compare_dbs(&a, &b));
    }

    let mut args = std::env::args().skip(1);
    let writers: i64 = args.next().and_then(|s| s.parse().ok()).unwrap_or(4);
    let per: i64 = args.next().and_then(|s| s.parse().ok()).unwrap_or(1000);

    let path = "/tmp/turso_mvcc_probe.db";
    // MVCC keeps a separate logical log (.db-log); remove all sidecars so a prior
    // run's state can't be mistaken for corruption.
    for suffix in ["", "-wal", "-shm", "-log"] {
        let _ = std::fs::remove_file(format!("{path}{suffix}"));
    }

    let io: Arc<dyn IO> = Arc::new(PlatformIO::new().unwrap());
    let db = Database::open_file(io.clone(), path).unwrap();

    let setup = db.connect().unwrap();
    exec(&setup, &io, "PRAGMA journal_mode = 'mvcc'").expect("enable mvcc");
    println!("mvcc_enabled = {}", db.mvcc_enabled());
    exec(&setup, &io, "CREATE TABLE t(id INTEGER PRIMARY KEY, w INTEGER, v TEXT)")
        .expect("create table");

    println!("launching {writers} concurrent writers x {per} rows (disjoint id ranges)...");

    let mut handles = Vec::new();
    for w in 0..writers {
        let db = db.clone();
        let io = io.clone();
        handles.push(thread::spawn(move || -> Result<(), String> {
            let conn = db.connect().map_err(|e| format!("w{w} connect: {e}"))?;
            exec(&conn, &io, "PRAGMA journal_mode = 'mvcc'")?;
            let base = w * per;
            exec(&conn, &io, "BEGIN CONCURRENT")?;
            for i in 0..per {
                let id = base + i + 1;
                exec(&conn, &io, &format!("INSERT INTO t(id, w, v) VALUES({id}, {w}, 'x{id}')"))?;
            }
            exec(&conn, &io, "COMMIT")?;
            Ok(())
        }));
    }

    let mut committed = 0;
    let mut errors = Vec::new();
    for (w, h) in handles.into_iter().enumerate() {
        match h.join().unwrap() {
            Ok(()) => committed += 1,
            Err(e) => errors.push(format!("writer {w}: {e}")),
        }
    }

    let in_mem = scalar(&setup, &io, "SELECT COUNT(*) FROM t").unwrap_or(-1);
    println!("in-memory count after commits: {in_mem}");

    // Flush the committed MVCC data to the main db file.
    println!("checkpointing (PRAGMA wal_checkpoint(TRUNCATE))...");
    match exec(&setup, &io, "PRAGMA wal_checkpoint(TRUNCATE)") {
        Ok(()) => println!("checkpoint returned OK"),
        Err(e) => println!("checkpoint ERROR: {e}"),
    }
    if let Ok(md) = std::fs::metadata(path) {
        println!("on-disk main file size after checkpoint: {} bytes", md.len());
    }

    let count = scalar(&setup, &io, "SELECT COUNT(*) FROM t").unwrap_or(-1);
    let expected = writers * per;

    println!("\n--- result ---");
    println!("writers committed : {committed}/{writers}");
    println!("rows in table      : {count} (expected {expected})");
    if !errors.is_empty() {
        println!("errors:");
        for e in &errors {
            println!("  {e}");
        }
    }
    if committed == writers && count == expected {
        println!("\nPASS: turso_core committed {writers} concurrent write transactions.");
    } else {
        println!("\nFAIL: concurrent writes did not all succeed (see above).");
        std::process::exit(1);
    }
}
