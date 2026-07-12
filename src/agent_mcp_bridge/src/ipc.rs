//! thoth-ipc transport: [`Bridge`] connects the four agent-control routes and
//! exposes act-and-observe operations. Calls are synchronous and serialized (one
//! in flight), so request/reply correlation is a simple "send, then read until the
//! matching `request_id`". The `mcp` server wraps a single `Bridge` on a dedicated
//! thread (Route is not assumed `Sync`).

use anyhow::{bail, Context, Result};
use libipc::channel::{Mode, Route};
use serde_json::{json, Value};
use std::time::{Duration, Instant};

use crate::protocol::{self, channel, Incoming};

const SEND_TIMEOUT_MS: u64 = 1000;
const RECV_POLL_MS: u64 = 200;
const OP_TIMEOUT: Duration = Duration::from_secs(5);

pub struct Bridge {
    cmd: Route,
    events: Route,
    state: Route,
    #[allow(dead_code)] // Phase C: get_frame reassembly
    frames: Route,
    snapshot: Route,
    next_id: u64,
}

impl Bridge {
    /// Connect to the default (unnamespaced) running Sourcetrail.
    pub fn connect() -> Result<Self> {
        Self::connect_instance("")
    }

    /// Connect to a running Sourcetrail whose agent-control channels are
    /// namespaced by `instance` (see `--agent-instance`). The bridge is the
    /// opposite end of every route: it *sends* commands and *receives* the rest.
    pub fn connect_instance(instance: &str) -> Result<Self> {
        let cmd = Route::connect(&channel::cmd(instance), Mode::Sender).with_context(|| {
            format!("connect {} (is Sourcetrail running with --agent-control?)", channel::cmd(instance))
        })?;
        let events = Route::connect(&channel::events(instance), Mode::Receiver)
            .with_context(|| format!("connect {}", channel::events(instance)))?;
        let state = Route::connect(&channel::state(instance), Mode::Receiver)
            .with_context(|| format!("connect {}", channel::state(instance)))?;
        let frames = Route::connect(&channel::frames(instance), Mode::Receiver)
            .with_context(|| format!("connect {}", channel::frames(instance)))?;
        let snapshot = Route::connect(&channel::snapshot(instance), Mode::Receiver)
            .with_context(|| format!("connect {}", channel::snapshot(instance)))?;
        Ok(Self { cmd, events, state, frames, snapshot, next_id: 1 })
    }

    fn next_id(&mut self) -> u64 {
        let id = self.next_id;
        self.next_id += 1;
        id
    }

    fn send(&mut self, bytes: &[u8]) -> Result<()> {
        self.cmd.send(bytes, SEND_TIMEOUT_MS).context("send command")?;
        Ok(())
    }

    /// Read `st.agent.events` until the `CommandResult` for `request_id`, keeping
    /// any `SearchCompleted` seen meanwhile. Returns `(ok, message, matches?)`.
    fn await_ack(&mut self, request_id: u64, timeout: Duration) -> Result<(bool, String, Option<Value>)> {
        let deadline = Instant::now() + timeout;
        let mut search: Option<Value> = None;
        while Instant::now() < deadline {
            let buf = self.events.recv(Some(RECV_POLL_MS)).context("recv event")?;
            if buf.is_empty() {
                continue;
            }
            let (_seq, incoming) = protocol::parse_event(buf.data())?;
            match incoming {
                Incoming::CommandResult { request_id: rid, ok, message } if rid == request_id => {
                    return Ok((ok, message, search));
                }
                Incoming::SearchCompleted(v) => search = Some(v),
                _ => {}
            }
        }
        bail!("timed out awaiting CommandResult for request {request_id}")
    }

    /// Read `st.agent.state` until the `UiStateEnvelope` for `request_id`.
    fn read_ui_state(&mut self, request_id: u64, timeout: Duration) -> Result<Value> {
        let deadline = Instant::now() + timeout;
        while Instant::now() < deadline {
            let buf = self.state.recv(Some(RECV_POLL_MS)).context("recv state")?;
            if buf.is_empty() {
                continue;
            }
            let (rid, json) = protocol::parse_ui_state(buf.data())?;
            if rid == request_id {
                return Ok(json);
            }
        }
        bail!("timed out awaiting UiState for request {request_id}")
    }

    /// Read `st.agent.snapshot` until the `UiSnapshot` for `request_id`.
    fn read_snapshot(&mut self, request_id: u64, timeout: Duration) -> Result<Value> {
        let deadline = Instant::now() + timeout;
        while Instant::now() < deadline {
            let buf = self.snapshot.recv(Some(RECV_POLL_MS)).context("recv snapshot")?;
            if buf.is_empty() {
                continue;
            }
            let (rid, json) = protocol::parse_ui_snapshot(buf.data())?;
            if rid == request_id {
                return Ok(json);
            }
        }
        bail!("timed out awaiting UiSnapshot for request {request_id}")
    }

    // --- operations (act + observe) -----------------------------------------

    pub fn get_ui_state(&mut self) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::get_ui_state(id))?;
        self.read_ui_state(id, OP_TIMEOUT)
    }

    /// Capture the structural UI tree (accessibility, or the raw object tree). The
    /// app acks on events, then sends the `UiSnapshot` on st.agent.snapshot (the
    /// capture hops to the GUI thread, so allow a generous window).
    pub fn get_snapshot(&mut self, object_tree: bool) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::get_snapshot(id, object_tree))?;
        let (ok, msg, _) = self.await_ack(id, OP_TIMEOUT)?;
        if !ok {
            bail!("get_snapshot rejected: {msg}");
        }
        self.read_snapshot(id, Duration::from_secs(15))
    }

    pub fn search(&mut self, query: &str) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::search(id, query))?;
        let (ok, msg, matches) = self.await_ack(id, OP_TIMEOUT)?;
        if !ok {
            bail!("search rejected: {msg}");
        }
        Ok(matches.unwrap_or_else(|| json!([])))
    }

    pub fn activate_node(&mut self, serialized_name: Option<&str>, node_id: u64) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::activate_node(id, serialized_name, node_id))?;
        let (ok, msg, _) = self.await_ack(id, OP_TIMEOUT)?;
        if !ok {
            bail!("activate_node rejected: {msg}");
        }
        self.get_ui_state()
    }

    pub fn activate_file(&mut self, path: &str) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::activate_file(id, path))?;
        let (ok, msg, _) = self.await_ack(id, OP_TIMEOUT)?;
        if !ok {
            bail!("activate_file rejected: {msg}");
        }
        self.get_ui_state()
    }

    pub fn activate_tab(&mut self, tab_id: u64) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::activate_tab(id, tab_id))?;
        let (ok, msg, _) = self.await_ack(id, OP_TIMEOUT)?;
        if !ok {
            bail!("activate_tab rejected: {msg}");
        }
        self.get_ui_state()
    }

    /// Loading kicks off indexing; return on ack and let the caller poll
    /// `app_state` (or subscribe) for the transition to `Ready`.
    pub fn load_project(&mut self, path: &str) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::load_project(id, path))?;
        let (ok, msg, _) = self.await_ack(id, Duration::from_secs(10))?;
        if !ok {
            bail!("load_project rejected: {msg}");
        }
        Ok(json!({ "ok": true, "message": msg }))
    }

    /// Resolve a human query to ranked element matches: `search`, then order by
    /// exact text > prefix > contains, then by score. The agent activates the
    /// first match's `node_ids[0]`.
    pub fn find_element(&mut self, query: &str) -> Result<Value> {
        let matches = self.search(query)?;
        let mut items: Vec<Value> = matches.as_array().cloned().unwrap_or_default();
        let q = query.to_lowercase();
        items.sort_by_key(|m| {
            let text = m.get("text").and_then(Value::as_str).unwrap_or("").to_lowercase();
            let rank = if text == q {
                0
            } else if text.starts_with(&q) {
                1
            } else if text.contains(&q) {
                2
            } else {
                3
            };
            let score = m.get("score").and_then(Value::as_i64).unwrap_or(0);
            (rank, -score) // lower rank first, higher score first
        });
        Ok(json!({ "query": query, "matches": items }))
    }
}
