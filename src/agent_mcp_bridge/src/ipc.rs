//! thoth-ipc transport: [`Bridge`] connects the four agent-control routes and
//! exposes act-and-observe operations. Calls are synchronous and serialized (one
//! in flight), so request/reply correlation is a simple "send, then read until the
//! matching `request_id`". The `mcp` server wraps a single `Bridge` on a dedicated
//! thread (Route is not assumed `Sync`).

use anyhow::{bail, Context, Result};
use libipc::channel::{Mode, Route};
use serde_json::{json, Value};
use std::collections::VecDeque;
use std::time::{Duration, Instant};

use crate::protocol::{self, channel, Incoming};

const SEND_TIMEOUT_MS: u64 = 1000;
const RECV_POLL_MS: u64 = 200;
const OP_TIMEOUT: Duration = Duration::from_secs(5);
/// Bound on the buffered event log (drops oldest; `seq` gaps let a poller detect it).
const EVENT_LOG_CAP: usize = 2048;

pub struct Bridge {
    cmd: Route,
    cmd_name: String,
    events: Route,
    state: Route,
    #[allow(dead_code)] // Phase C: get_frame reassembly
    frames: Route,
    snapshot: Route,
    next_id: u64,
    // Observation buffer: every event read off st.agent.events (during acks or an
    // explicit drain) is recorded here as self-describing JSON, so poll_events can
    // replay it by seq cursor. Bounded; oldest dropped.
    event_log: VecDeque<Value>,
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
        let cmd_name = channel::cmd(instance);
        let cmd = Route::connect(&cmd_name, Mode::Sender).with_context(|| {
            format!("connect {cmd_name} (is Sourcetrail running with --agent-control?)")
        })?;
        let events = Route::connect(&channel::events(instance), Mode::Receiver)
            .with_context(|| format!("connect {}", channel::events(instance)))?;
        let state = Route::connect(&channel::state(instance), Mode::Receiver)
            .with_context(|| format!("connect {}", channel::state(instance)))?;
        let frames = Route::connect(&channel::frames(instance), Mode::Receiver)
            .with_context(|| format!("connect {}", channel::frames(instance)))?;
        let snapshot = Route::connect(&channel::snapshot(instance), Mode::Receiver)
            .with_context(|| format!("connect {}", channel::snapshot(instance)))?;
        Ok(Self { cmd, cmd_name, events, state, frames, snapshot, next_id: 1, event_log: VecDeque::new() })
    }

    fn next_id(&mut self) -> u64 {
        let id = self.next_id;
        self.next_id += 1;
        id
    }

    fn send(&mut self, bytes: &[u8]) -> Result<()> {
        // `send` returns Ok(false) when no receiver consumed the frame within the
        // timeout (e.g. the app isn't listening). Surface that as a distinct error
        // instead of proceeding to a misleading reply-wait timeout downstream.
        let delivered = self.cmd.send(bytes, SEND_TIMEOUT_MS).context("send command")?;
        if !delivered {
            bail!(
                "command not delivered on {}: no agent-control receiver consumed it \
                 (is Sourcetrail running and listening on this channel?)",
                self.cmd_name
            );
        }
        Ok(())
    }

    /// Record one raw `EventEnvelope` into the observation log (bounded ring).
    /// Called wherever events are read off `st.agent.events`, so nothing consumed
    /// for reply correlation is lost to `poll_events`.
    fn record_event(&mut self, bytes: &[u8]) {
        if let Ok(v) = protocol::event_to_json(bytes) {
            if self.event_log.len() >= EVENT_LOG_CAP {
                self.event_log.pop_front();
            }
            self.event_log.push_back(v);
        }
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
            self.record_event(buf.data());
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

    /// The shared act-and-observe prefix: assign a request id, send the built
    /// command, await its `CommandResult`, and bail with a uniform message on
    /// rejection. Returns `(id, ack_message)` so the caller can then read its own
    /// reply (`get_ui_state` / `read_snapshot`) or surface the ack. `search` opts
    /// out — its result races the ack and needs a bespoke loop.
    fn send_and_ack(
        &mut self,
        label: &str,
        timeout: Duration,
        build: impl FnOnce(u64) -> Vec<u8>,
    ) -> Result<(u64, String)> {
        let id = self.next_id();
        self.send(&build(id))?;
        let (ok, msg, _) = self.await_ack(id, timeout)?;
        if !ok {
            bail!("{label} rejected: {msg}");
        }
        Ok((id, msg))
    }

    /// Read events (recording all meanwhile) until the `Settled` for `request_id`
    /// — the queue-idle barrier that means the command's synchronous fan-out has
    /// drained, so the next read observes the *settled* state.
    fn await_settled(&mut self, request_id: u64, timeout: Duration) -> Result<()> {
        let deadline = Instant::now() + timeout;
        while Instant::now() < deadline {
            let buf = self.events.recv(Some(RECV_POLL_MS)).context("recv event")?;
            if buf.is_empty() {
                continue;
            }
            self.record_event(buf.data());
            if let Incoming::Settled { request_id: rid } = protocol::parse_event(buf.data())?.1 {
                if rid == request_id {
                    return Ok(());
                }
            }
        }
        bail!("timed out awaiting Settled for request {request_id}")
    }

    /// Full act-and-observe: send, await the ack (reject → error), await `Settled`
    /// so the effect has landed, then read the settled `UiState`. The general
    /// contract for mutating commands — no per-command race handling.
    fn act_and_observe(&mut self, label: &str, build: impl FnOnce(u64) -> Vec<u8>) -> Result<Value> {
        let (id, _) = self.send_and_ack(label, OP_TIMEOUT, build)?;
        self.await_settled(id, OP_TIMEOUT)?;
        self.get_ui_state()
    }

    // --- operations (act + observe) -----------------------------------------

    /// Read-only channel health — no side effects. `cmd`'s receiver count is the
    /// number of apps listening (this bridge is the sender there); the reply
    /// channels' counts include this bridge itself. Handy for "is the app up? are
    /// there stray receivers?" without a round-trip.
    pub fn status(&self) -> Value {
        json!({
            "app_listening": self.cmd.recv_count() >= 1,
            "channel_receivers": {
                "cmd": self.cmd.recv_count(),        // = app listeners
                "events": self.events.recv_count(),  // reply channels: count includes this bridge
                "state": self.state.recv_count(),
                "snapshot": self.snapshot.recv_count(),
                "frames": self.frames.recv_count(),
            },
        })
    }

    /// Observation stream: drain any pending events (non-blocking) into the log,
    /// then return those with `seq > since_seq` plus `latest_seq` as the next
    /// cursor. `{ events: [...], latest_seq }`. A jump in `seq` past the returned
    /// events means some were dropped (the log and the shm ring are both bounded)
    /// — poll more often. Start from `since_seq = 0`.
    pub fn poll_events(&mut self, since_seq: u64) -> Value {
        for _ in 0..EVENT_LOG_CAP {
            match self.events.try_recv() {
                Ok(buf) if !buf.is_empty() => self.record_event(buf.data()),
                _ => break,
            }
        }
        let events: Vec<Value> = self
            .event_log
            .iter()
            .filter(|e| e.get("seq").and_then(Value::as_u64).is_some_and(|s| s > since_seq))
            .cloned()
            .collect();
        let latest = self
            .event_log
            .back()
            .and_then(|e| e.get("seq"))
            .and_then(Value::as_u64)
            .unwrap_or(since_seq);
        json!({ "events": events, "latest_seq": latest })
    }

    pub fn get_ui_state(&mut self) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::get_ui_state(id))?;
        self.read_ui_state(id, OP_TIMEOUT)
    }

    /// Capture the structural UI tree (accessibility, or the raw object tree). The
    /// app acks on events, then sends the `UiSnapshot` on st.agent.snapshot (the
    /// capture hops to the GUI thread, so allow a generous window).
    pub fn get_snapshot(&mut self, object_tree: bool) -> Result<Value> {
        let (id, _) =
            self.send_and_ack("get_snapshot", OP_TIMEOUT, |id| protocol::get_snapshot(id, object_tree))?;
        self.read_snapshot(id, Duration::from_secs(15))
    }

    /// Search has a two-part reply: a `CommandResult` ack (which may reject, e.g.
    /// no project) and a `SearchCompleted` event carrying the matches. The app runs
    /// the query on its message thread and emits the result *after* `handleCommand`
    /// returns, so `SearchCompleted` can arrive after the ack. Read past the ack
    /// until the matches land, rather than returning on the ack (which raced to an
    /// empty result).
    pub fn search(&mut self, query: &str) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::search(id, query))?;

        let deadline = Instant::now() + OP_TIMEOUT;
        let mut acked = false;
        while Instant::now() < deadline {
            let buf = self.events.recv(Some(RECV_POLL_MS)).context("recv event")?;
            if buf.is_empty() {
                continue;
            }
            self.record_event(buf.data());
            let (_seq, incoming) = protocol::parse_event(buf.data())?;
            match incoming {
                Incoming::SearchCompleted(v) => return Ok(v),
                Incoming::CommandResult { request_id: rid, ok, message } if rid == id => {
                    if !ok {
                        bail!("search rejected: {message}");
                    }
                    acked = true; // accepted — the SearchCompleted event is on its way
                }
                _ => {}
            }
        }
        if acked {
            bail!("search accepted but no results within {OP_TIMEOUT:?} (query too broad?)");
        }
        bail!("timed out awaiting search reply for request {id}")
    }

    pub fn activate_node(&mut self, serialized_name: Option<&str>, node_id: u64) -> Result<Value> {
        self.act_and_observe("activate_node", |id| protocol::activate_node(id, serialized_name, node_id))
    }

    pub fn activate_file(&mut self, path: &str) -> Result<Value> {
        self.act_and_observe("activate_file", |id| protocol::activate_file(id, path))
    }

    pub fn activate_tab(&mut self, tab_id: u64) -> Result<Value> {
        self.act_and_observe("activate_tab", |id| protocol::activate_tab(id, tab_id))
    }

    /// Structural control: invoke an accessible `action` (or set `text`) on the
    /// element addressed by a snapshot `ref` (object_name + role/name/index path).
    /// The app resolves + invokes on the GUI thread; the ack **is** the result, and
    /// resolution failures ("element not found", "action not supported") come back
    /// as data (`{ ok, message }`), not transport errors.
    pub fn invoke_action(
        &mut self,
        object_name: &str,
        path: &[(String, String, u32)],
        action: &str,
        text: &str,
    ) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::invoke_action(id, object_name, path, action, text))?;
        let (ok, message, _) = self.await_ack(id, OP_TIMEOUT)?;
        Ok(json!({ "ok": ok, "message": message }))
    }

    /// Loading kicks off indexing; return on ack and let the caller poll
    /// `app_state` (or subscribe) for the transition to `Ready`.
    pub fn load_project(&mut self, path: &str) -> Result<Value> {
        let (_, msg) =
            self.send_and_ack("load_project", Duration::from_secs(10), |id| protocol::load_project(id, path))?;
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
