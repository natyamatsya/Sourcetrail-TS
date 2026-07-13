//! A small, `tsq_`-prefixed C API over `turso_core`.
//!
//! This is the firewall that lets Turso run *alongside* real SQLite in one
//! binary: Turso's own C bindings export `sqlite3_*` symbols that would collide
//! with libsqlite3, so instead we re-export only the ~20 primitives that
//! Sourcetrail's `CppSQLite3` wrapper actually uses, under a `tsq_` prefix.
//!
//! Result and column-type codes deliberately mirror SQLite's so the C++ side can
//! treat both engines uniformly. The C++ `turso::CppTurso3` wrapper is a
//! method-for-method mirror of `CppSQLite3` that calls these functions.

use std::cell::RefCell;
use std::ffi::{c_char, c_int, c_longlong, CStr, CString};
use std::num::NonZero;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::sync::Arc;

use turso_core::types::{Value, ValueType};
use turso_core::{Connection, Database, LimboError, PlatformIO, Statement, StepResult, IO};

// ---- Result codes (mirror sqlite3.h) --------------------------------------
pub const TSQ_OK: c_int = 0;
pub const TSQ_ERROR: c_int = 1;
pub const TSQ_BUSY: c_int = 5; // transient MVCC busy/conflict; retryable
pub const TSQ_ROW: c_int = 100;
pub const TSQ_DONE: c_int = 101;

// ---- Column types (mirror sqlite3.h) --------------------------------------
const TSQ_INTEGER: c_int = 1;
const TSQ_FLOAT: c_int = 2;
const TSQ_TEXT: c_int = 3;
const TSQ_BLOB: c_int = 4;
const TSQ_NULL: c_int = 5;

thread_local! {
    static LAST_ERR: RefCell<CString> = RefCell::new(CString::default());
    static LAST_CODE: std::cell::Cell<c_int> = const { std::cell::Cell::new(TSQ_OK) };
}

fn set_err(msg: impl AsRef<str>) {
    set_err_code(msg, TSQ_ERROR);
}

fn set_err_code(msg: impl AsRef<str>, code: c_int) {
    // CString cannot hold interior NULs; sanitise defensively.
    let cleaned = msg.as_ref().replace('\0', " ");
    let c = CString::new(cleaned).unwrap_or_else(|_| CString::new("error").unwrap());
    LAST_ERR.with(|e| *e.borrow_mut() = c);
    LAST_CODE.with(|c| c.set(code));
}

/// (code, message) for a turso_core error: transient busy/conflict conditions —
/// the ones a `BEGIN CONCURRENT` writer should roll back and retry — map to
/// TSQ_BUSY, everything else to TSQ_ERROR.
fn classify(e: LimboError) -> (c_int, String) {
    let code = match &e {
        LimboError::Busy
        | LimboError::BusySnapshot
        | LimboError::Conflict(_)
        | LimboError::WriteWriteConflict
        | LimboError::CommitDependencyAborted => TSQ_BUSY,
        _ => TSQ_ERROR,
    };
    (code, e.to_string())
}

/// Owns the Turso database, a default connection, and the IO object we must
/// drive on `StepResult::IO`. Additional connections (for concurrent MVCC
/// writers) are created from the same `Database` via `tsq_new_connection`.
pub struct TsqDb {
    conn: Arc<Connection>,
    db: Arc<Database>,
    io: Arc<dyn IO>,
}

/// An additional connection on a shared `Database`. Multiple `TsqConn`s to the
/// same `TsqDb` can hold concurrent `BEGIN CONCURRENT` write transactions when
/// the database is in MVCC mode (`PRAGMA journal_mode = 'mvcc'`).
pub struct TsqConn {
    conn: Arc<Connection>,
    io: Arc<dyn IO>,
}

/// A decoded column value, kept alive on the statement so C can hold pointers to
/// it until the next step/reset. `text` is always NUL-terminated so it can back
/// `sqlite3_column_text`-style access directly.
struct Cell {
    typ: c_int,
    int: i64,
    real: f64,
    text: Vec<u8>, // NUL-terminated UTF-8 rendering (numbers rendered like sqlite)
    blob: Vec<u8>,
}

impl Cell {
    fn null() -> Self {
        Cell { typ: TSQ_NULL, int: 0, real: 0.0, text: vec![0], blob: Vec::new() }
    }
}

pub struct TsqStmt {
    stmt: Statement,
    io: Arc<dyn IO>,
    cols: Vec<Cell>,
}

fn cstr_bytes(s: &str) -> Vec<u8> {
    let mut v = s.as_bytes().to_vec();
    v.push(0);
    v
}

/// Best-effort leading-integer parse, matching how sqlite coerces TEXT -> INT.
fn parse_int_prefix(s: &str) -> i64 {
    let s = s.trim_start();
    let mut end = 0;
    let bytes = s.as_bytes();
    if end < bytes.len() && (bytes[end] == b'-' || bytes[end] == b'+') {
        end += 1;
    }
    while end < bytes.len() && bytes[end].is_ascii_digit() {
        end += 1;
    }
    s[..end].parse().unwrap_or(0)
}

fn decode(v: &Value) -> Cell {
    // Use only Value's public accessors (the Numeric enum is private).
    let mut c = Cell::null();
    match v.value_type() {
        ValueType::Integer => {
            let i = v.as_int().unwrap_or(0);
            c.typ = TSQ_INTEGER;
            c.int = i;
            c.real = i as f64;
            c.text = cstr_bytes(&i.to_string());
        }
        ValueType::Float => {
            let fv = v.to_float_or_zero();
            c.typ = TSQ_FLOAT;
            c.real = fv;
            c.int = fv as i64;
            c.text = cstr_bytes(&format!("{fv}"));
        }
        ValueType::Text => {
            let s = v.to_text().unwrap_or("");
            c.typ = TSQ_TEXT;
            c.text = cstr_bytes(s);
            c.int = parse_int_prefix(s);
            c.real = s.trim().parse().unwrap_or(0.0);
        }
        ValueType::Blob => {
            let b = v.to_blob().unwrap_or(&[]);
            c.typ = TSQ_BLOB;
            c.blob = b.to_vec();
            let mut t = b.to_vec();
            t.push(0);
            c.text = t;
        }
        ValueType::Null | ValueType::Error => {}
    }
    c
}

// ---------------------------------------------------------------------------
// FFI surface. Every function is null-safe and panic-safe at the boundary.
// ---------------------------------------------------------------------------

/// Returns the last error message for this thread (never null). Valid until the
/// next failing `tsq_*` call on the same thread; copy immediately.
#[no_mangle]
pub extern "C" fn tsq_last_error() -> *const c_char {
    LAST_ERR.with(|e| e.borrow().as_ptr())
}

/// Classifies the last error for this thread: TSQ_BUSY for transient MVCC
/// busy/conflict errors (roll back and retry the transaction), TSQ_ERROR for
/// everything else. Valid under the same rules as `tsq_last_error`.
#[no_mangle]
pub extern "C" fn tsq_last_error_code() -> c_int {
    LAST_CODE.with(|c| c.get())
}

#[no_mangle]
pub extern "C" fn tsq_open(path: *const c_char) -> *mut TsqDb {
    let r = catch_unwind(AssertUnwindSafe(|| {
        let path = unsafe { CStr::from_ptr(path) }
            .to_str()
            .map_err(|e| e.to_string())?;
        let io: Arc<dyn IO> = Arc::new(PlatformIO::new().map_err(|e| e.to_string())?);
        let db = Database::open_file(io.clone(), path).map_err(|e| e.to_string())?;
        let conn = db.connect().map_err(|e| e.to_string())?;
        Ok::<Box<TsqDb>, String>(Box::new(TsqDb { conn, db, io }))
    }));
    match r {
        Ok(Ok(b)) => Box::into_raw(b),
        Ok(Err(e)) => {
            set_err(e);
            ptr::null_mut()
        }
        Err(_) => {
            set_err("panic in tsq_open");
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn tsq_close(db: *mut TsqDb) {
    if !db.is_null() {
        unsafe { drop(Box::from_raw(db)) };
    }
}

/// Runs one or more `;`-separated statements to completion (like sqlite3_exec).
#[no_mangle]
pub extern "C" fn tsq_exec(db: *mut TsqDb, sql: *const c_char) -> c_int {
    if db.is_null() {
        return TSQ_ERROR;
    }
    let db = unsafe { &*db };
    let r = catch_unwind(AssertUnwindSafe(|| {
        let sql = unsafe { CStr::from_ptr(sql) }
            .to_str()
            .map_err(|e| (TSQ_ERROR, e.to_string()))?;
        db.conn.prepare_execute_batch(sql).map_err(classify)
    }));
    match r {
        Ok(Ok(())) => TSQ_OK,
        Ok(Err((code, msg))) => {
            set_err_code(msg, code);
            TSQ_ERROR
        }
        Err(_) => {
            set_err("panic in tsq_exec");
            TSQ_ERROR
        }
    }
}

#[no_mangle]
pub extern "C" fn tsq_prepare(db: *mut TsqDb, sql: *const c_char) -> *mut TsqStmt {
    if db.is_null() {
        return ptr::null_mut();
    }
    let db = unsafe { &*db };
    let r = catch_unwind(AssertUnwindSafe(|| {
        let sql = unsafe { CStr::from_ptr(sql) }
            .to_str()
            .map_err(|e| e.to_string())?;
        let stmt = db.conn.prepare(sql).map_err(|e| e.to_string())?;
        Ok::<Box<TsqStmt>, String>(Box::new(TsqStmt {
            stmt,
            io: db.io.clone(),
            cols: Vec::new(),
        }))
    }));
    match r {
        Ok(Ok(b)) => Box::into_raw(b),
        Ok(Err(e)) => {
            set_err(e);
            ptr::null_mut()
        }
        Err(_) => {
            set_err("panic in tsq_prepare");
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn tsq_bind_int(stmt: *mut TsqStmt, idx: c_int, val: c_longlong) -> c_int {
    if stmt.is_null() {
        return TSQ_ERROR;
    }
    let s = unsafe { &mut *stmt };
    let Some(nz) = NonZero::new(idx as usize) else {
        set_err("bind index must be >= 1");
        return TSQ_ERROR;
    };
    match s.stmt.bind_at(nz, Value::from_i64(val as i64)) {
        Ok(()) => TSQ_OK,
        Err(e) => {
            set_err(e.to_string());
            TSQ_ERROR
        }
    }
}

#[no_mangle]
pub extern "C" fn tsq_bind_text(
    stmt: *mut TsqStmt,
    idx: c_int,
    ptr: *const c_char,
    len: c_int,
) -> c_int {
    if stmt.is_null() {
        return TSQ_ERROR;
    }
    let s = unsafe { &mut *stmt };
    let Some(nz) = NonZero::new(idx as usize) else {
        set_err("bind index must be >= 1");
        return TSQ_ERROR;
    };
    let bytes = if ptr.is_null() || len < 0 {
        &[][..]
    } else {
        unsafe { std::slice::from_raw_parts(ptr as *const u8, len as usize) }
    };
    let text = String::from_utf8_lossy(bytes).into_owned();
    match s.stmt.bind_at(nz, Value::build_text(text)) {
        Ok(()) => TSQ_OK,
        Err(e) => {
            set_err(e.to_string());
            TSQ_ERROR
        }
    }
}

/// Advances the statement, driving IO as needed. Returns TSQ_ROW / TSQ_DONE /
/// TSQ_ERROR. On TSQ_ROW the current row is decoded into `cols`.
#[no_mangle]
pub extern "C" fn tsq_step(stmt: *mut TsqStmt) -> c_int {
    if stmt.is_null() {
        return TSQ_ERROR;
    }
    let s = unsafe { &mut *stmt };
    let r = catch_unwind(AssertUnwindSafe(|| {
        loop {
            match s.stmt.step().map_err(classify)? {
                StepResult::Row => {
                    let n = s.stmt.num_columns();
                    let mut cols = Vec::with_capacity(n);
                    {
                        let row = s
                            .stmt
                            .row()
                            .ok_or_else(|| (TSQ_ERROR, "row missing".to_string()))?;
                        for i in 0..n {
                            cols.push(decode(row.get_value(i)));
                        }
                    }
                    s.cols = cols;
                    return Ok::<c_int, (c_int, String)>(TSQ_ROW);
                }
                StepResult::Done => {
                    s.cols.clear();
                    return Ok(TSQ_DONE);
                }
                // Drive IO and retry. Busy is treated the same in this
                // single-connection comparison harness.
                StepResult::IO | StepResult::Yield | StepResult::Busy => {
                    s.io.step().map_err(classify)?;
                }
                StepResult::Interrupt => {
                    set_err("interrupted");
                    return Ok(TSQ_ERROR);
                }
            }
        }
    }));
    match r {
        Ok(Ok(code)) => code,
        Ok(Err((code, msg))) => {
            set_err_code(msg, code);
            TSQ_ERROR
        }
        Err(_) => {
            set_err("panic in tsq_step");
            TSQ_ERROR
        }
    }
}

#[no_mangle]
pub extern "C" fn tsq_reset(stmt: *mut TsqStmt) -> c_int {
    if stmt.is_null() {
        return TSQ_ERROR;
    }
    let s = unsafe { &mut *stmt };
    s.cols.clear();
    match s.stmt.reset() {
        Ok(()) => TSQ_OK,
        Err(e) => {
            set_err(e.to_string());
            TSQ_ERROR
        }
    }
}

#[no_mangle]
pub extern "C" fn tsq_finalize(stmt: *mut TsqStmt) {
    if !stmt.is_null() {
        unsafe { drop(Box::from_raw(stmt)) };
    }
}

#[no_mangle]
pub extern "C" fn tsq_column_count(stmt: *mut TsqStmt) -> c_int {
    if stmt.is_null() {
        return 0;
    }
    let s = unsafe { &*stmt };
    s.stmt.num_columns() as c_int
}

#[no_mangle]
pub extern "C" fn tsq_column_type(stmt: *mut TsqStmt, i: c_int) -> c_int {
    if stmt.is_null() || i < 0 {
        return TSQ_NULL;
    }
    let s = unsafe { &*stmt };
    s.cols.get(i as usize).map_or(TSQ_NULL, |c| c.typ)
}

#[no_mangle]
pub extern "C" fn tsq_column_int(stmt: *mut TsqStmt, i: c_int) -> c_longlong {
    if stmt.is_null() || i < 0 {
        return 0;
    }
    let s = unsafe { &*stmt };
    s.cols.get(i as usize).map_or(0, |c| c.int) as c_longlong
}

/// Returns a NUL-terminated pointer valid until the next step/reset/finalize, or
/// null for SQL NULL (so the caller can substitute its default).
#[no_mangle]
pub extern "C" fn tsq_column_text(stmt: *mut TsqStmt, i: c_int) -> *const c_char {
    if stmt.is_null() || i < 0 {
        return ptr::null();
    }
    let s = unsafe { &*stmt };
    match s.cols.get(i as usize) {
        Some(c) if c.typ != TSQ_NULL => c.text.as_ptr() as *const c_char,
        _ => ptr::null(),
    }
}

#[no_mangle]
pub extern "C" fn tsq_last_insert_rowid(db: *mut TsqDb) -> c_longlong {
    if db.is_null() {
        return 0;
    }
    let db = unsafe { &*db };
    db.conn.last_insert_rowid() as c_longlong
}

#[no_mangle]
pub extern "C" fn tsq_changes(db: *mut TsqDb) -> c_longlong {
    if db.is_null() {
        return 0;
    }
    let db = unsafe { &*db };
    db.conn.changes() as c_longlong
}

#[no_mangle]
pub extern "C" fn tsq_interrupt(db: *mut TsqDb) {
    if db.is_null() {
        return;
    }
    let db = unsafe { &*db };
    db.conn.interrupt();
}

// ---------------------------------------------------------------------------
// Additional connections on a shared Database — for concurrent MVCC writers.
// Each TsqConn is an independent connection; with `BEGIN CONCURRENT` on an MVCC
// database, several can hold write transactions at once. Statements returned by
// tsq_conn_prepare use the shared statement API (tsq_bind_*/step/column/etc.).
// ---------------------------------------------------------------------------

/// Open an additional connection on the same underlying Database. Returns null
/// on failure (see tsq_last_error).
#[no_mangle]
pub extern "C" fn tsq_new_connection(db: *mut TsqDb) -> *mut TsqConn {
    if db.is_null() {
        return ptr::null_mut();
    }
    let db = unsafe { &*db };
    let r = catch_unwind(AssertUnwindSafe(|| {
        let conn = db.db.connect().map_err(|e| e.to_string())?;
        Ok::<Box<TsqConn>, String>(Box::new(TsqConn { conn, io: db.io.clone() }))
    }));
    match r {
        Ok(Ok(b)) => Box::into_raw(b),
        Ok(Err(e)) => {
            set_err(e);
            ptr::null_mut()
        }
        Err(_) => {
            set_err("panic in tsq_new_connection");
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn tsq_conn_close(conn: *mut TsqConn) {
    if !conn.is_null() {
        unsafe { drop(Box::from_raw(conn)) };
    }
}

#[no_mangle]
pub extern "C" fn tsq_conn_exec(conn: *mut TsqConn, sql: *const c_char) -> c_int {
    if conn.is_null() {
        return TSQ_ERROR;
    }
    let c = unsafe { &*conn };
    let r = catch_unwind(AssertUnwindSafe(|| {
        let sql = unsafe { CStr::from_ptr(sql) }
            .to_str()
            .map_err(|e| (TSQ_ERROR, e.to_string()))?;
        c.conn.prepare_execute_batch(sql).map_err(classify)
    }));
    match r {
        Ok(Ok(())) => TSQ_OK,
        Ok(Err((code, msg))) => {
            set_err_code(msg, code);
            TSQ_ERROR
        }
        Err(_) => {
            set_err("panic in tsq_conn_exec");
            TSQ_ERROR
        }
    }
}

#[no_mangle]
pub extern "C" fn tsq_conn_prepare(conn: *mut TsqConn, sql: *const c_char) -> *mut TsqStmt {
    if conn.is_null() {
        return ptr::null_mut();
    }
    let c = unsafe { &*conn };
    let r = catch_unwind(AssertUnwindSafe(|| {
        let sql = unsafe { CStr::from_ptr(sql) }
            .to_str()
            .map_err(|e| e.to_string())?;
        let stmt = c.conn.prepare(sql).map_err(|e| e.to_string())?;
        Ok::<Box<TsqStmt>, String>(Box::new(TsqStmt {
            stmt,
            io: c.io.clone(),
            cols: Vec::new(),
        }))
    }));
    match r {
        Ok(Ok(b)) => Box::into_raw(b),
        Ok(Err(e)) => {
            set_err(e);
            ptr::null_mut()
        }
        Err(_) => {
            set_err("panic in tsq_conn_prepare");
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn tsq_conn_last_insert_rowid(conn: *mut TsqConn) -> c_longlong {
    if conn.is_null() {
        return 0;
    }
    let c = unsafe { &*conn };
    c.conn.last_insert_rowid() as c_longlong
}

#[no_mangle]
pub extern "C" fn tsq_conn_changes(conn: *mut TsqConn) -> c_longlong {
    if conn.is_null() {
        return 0;
    }
    let c = unsafe { &*conn };
    c.conn.changes() as c_longlong
}
