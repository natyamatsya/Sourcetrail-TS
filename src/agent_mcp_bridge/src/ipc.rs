//! thoth-ipc transport: [`Bridge`] connects the four agent-control routes and
//! exposes act-and-observe operations. Calls are synchronous and serialized (one
//! in flight), so request/reply correlation is a simple "send, then read until the
//! matching `request_id`". The `mcp` server wraps a single `Bridge` on a dedicated
//! thread (Route is not assumed `Sync`).

use anyhow::{anyhow, bail, Context, Result};
use thoth_ipc::channel::{Mode, Route};
use serde_json::{json, Value};
use std::collections::VecDeque;
use std::time::{Duration, Instant};

use crate::machine_view::{self, MachineView};
use crate::protocol::{self, channel, Incoming};

const SEND_TIMEOUT_MS: u64 = 1000;
const RECV_POLL_MS: u64 = 200;
const OP_TIMEOUT: Duration = Duration::from_secs(5);
/// Reply window for the two commands that capture the accessibility tree on the
/// GUI thread. Deliberately one constant: `query_ui` does everything
/// `get_snapshot` does — the same full-tree walk — and then compiles a JSONPath,
/// evaluates it and converts every match, so it is never the faster of the two.
/// It used to be given OP_TIMEOUT while get_snapshot had 15s, which showed up as
/// query_ui timing out intermittently on exactly the queries that match a lot.
const SNAPSHOT_TIMEOUT: Duration = Duration::from_secs(15);
/// How long `load_project` waits for a load to finish (a large index takes a while
/// to open), and how often it looks.
const LOAD_TIMEOUT: Duration = Duration::from_secs(300);
const LOAD_POLL: Duration = Duration::from_millis(100);
/// How long the app may go without answering before a wait gives up on it. A
/// reply may take longer while the app answers: its queue is busy, and the reply
/// comes once the queue gets to it.
const UNRESPONSIVE: Duration = Duration::from_secs(3);
/// How often a wait for a reply asks whether the app still answers.
const PROBE_EVERY: Duration = Duration::from_secs(1);
/// The longest a reply may take while the app answers, so a queue that never gets
/// to it (stuck behind a live reader thread) still ends the wait.
const BUSY_LIMIT: Duration = Duration::from_secs(120);
/// Bound on the buffered event log (drops oldest; `seq` gaps let a poller detect it).
const EVENT_LOG_CAP: usize = 2048;
const REQUEST_LOG_CAP: usize = 256;

fn now_ms() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0)
}

/// An action this bridge sent, for the request log: what it was and when.
struct SentRequest {
    id: u64,
    command: &'static str,
    sent_ms: u64,
}

/// Pull the `thoth-ipc:<version>` token out of a build_id (`git:<hash>+thoth-ipc:X`).
/// That token is the SHM wire the two sides must agree on, so it's what we compare.
fn thoth_token(build_id: &str) -> Option<&str> {
    build_id.split('+').find_map(|t| t.strip_prefix("thoth-ipc:"))
}

/// "189,175 scale 1" (the tail of a v5 GetFrame ack) as (x, y, scale).
fn parse_frame_geometry(text: &str) -> Option<(f64, f64, f64)> {
    let (origin, scale) = text.trim().split_once(" scale ")?;
    let (x, y) = origin.split_once(',')?;
    let scale: f64 = scale.trim().parse().ok()?;
    (scale > 0.0).then_some((x.trim().parse().ok()?, y.trim().parse().ok()?, scale))
}

/// The error for a command the app refused. The app's own refusals already start
/// with "rejected: ...", so say it once rather than "X rejected: rejected: ...".
fn rejection(label: &str, message: &str) -> anyhow::Error {
    if message.starts_with("rejected") {
        anyhow!("{label} {message}")
    } else {
        anyhow!("{label} rejected: {message}")
    }
}

/// Whether the app still answers while a reply is slow to come. The state
/// machines answer on the app's reader thread, whatever its message thread is busy
/// with, so a `GetMachines` probe tells a busy app (keep waiting) from a hung or
/// gone one (give up after UNRESPONSIVE). Off for an app without state machines
/// and for a window shorter than PROBE_EVERY: those windows are taken as given.
struct Liveness {
    on: bool,
    // The reply waits in the app's queue, which a question parks until answered.
    queued: bool,
    deadline: Instant,
    // When the app last answered a probe (or the wait began).
    heard: Instant,
    // The probe in flight and when it went out.
    probe: Option<(u64, Instant)>,
}

/// A wait that gave up because the app did not answer, or answered everything but
/// the reply: the message, and what its state machines were doing as the bridge
/// last saw them (`machine_view::MachineView::report`). The MCP server hands
/// `report` to the client as the error's data.
#[derive(Debug)]
pub struct AppNotAnswering {
    pub message: String,
    pub report: Value,
}

impl std::fmt::Display for AppNotAnswering {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for AppNotAnswering {}

/// A reply that waits in the app's queue while a question holds it: the question
/// is to be answered first. Its own type, so a load that looks for the question
/// next tells it from an app that stopped answering.
#[derive(Debug)]
struct BehindQuestion(String);

impl std::fmt::Display for BehindQuestion {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for BehindQuestion {}

/// The ack and the `MachineStates` of a machine command, with the transitions
/// read meanwhile.
struct MachineReply {
    ok: bool,
    message: String,
    machines: Vec<Value>,
    transitions: Vec<Value>,
}

pub struct Bridge {
    cmd: Route,
    cmd_name: String,
    events: Route,
    state: Route,
    frames: Route,
    snapshot: Route,
    next_id: u64,
    // Observation buffer: every event read off st.agent.events (during acks or an
    // explicit drain) is recorded here as self-describing JSON, so poll_events can
    // replay it by seq cursor. Bounded; oldest dropped.
    event_log: VecDeque<Value>,
    // The actions sent (not the reads), newest last, for request_log(). Bounded;
    // oldest dropped.
    requests: VecDeque<SentRequest>,
    // Result of the connect-time protocol handshake (GetInfo -> AppInfo): the app's
    // protocol version + build identity and the computed skew, or an error note if
    // the handshake couldn't complete (app down / too old). Best-effort — never
    // fails connect. Surfaced via status() and the get_instance_info tool.
    handshake: Value,
    // How many frames were dropped as unparseable. Recovery is silent by design
    // (a bad frame is skipped, not fatal), so this is the only trace it leaves —
    // surfaced by status() so a corrupt channel is diagnosable rather than just
    // slow.
    dropped_frames: u64,
    // Where the last ui_frame sat in global coordinates (origin x, origin y,
    // scale): frame pixel p is the global point origin + p / scale. ui_input's
    // `at_frame` converts with it.
    last_frame: Option<(f64, f64, f64)>,
    // The last command sent: its id, and whether it waits in the app's queue.
    last_sent: Option<(u64, bool)>,
    // The app's state machines as the bridge last saw them (see machine_view):
    // what a "not answering" error reports the app was busy with.
    machines: MachineView,
    // Liveness probes whose replies have not been read yet. Their replies are the
    // bridge's own business and stay out of the event log.
    probes: VecDeque<u64>,
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
        let mut bridge = Self {
            cmd,
            cmd_name,
            events,
            state,
            frames,
            snapshot,
            next_id: 1,
            event_log: VecDeque::new(),
            requests: VecDeque::new(),
            handshake: Value::Null,
            dropped_frames: 0,
            last_frame: None,
            last_sent: None,
            machines: MachineView::default(),
            probes: VecDeque::new(),
        };
        // Best-effort handshake: detect protocol skew up front, but never fail the
        // connect over it (status/read-only use must work against a down or old app).
        bridge.handshake = bridge.perform_handshake();
        Ok(bridge)
    }

    /// The connect-time handshake result (see the `handshake` field).
    pub fn handshake(&self) -> Value {
        self.handshake.clone()
    }

    /// The app's agent-control protocol version from the handshake (0 if unknown).
    pub fn app_protocol_version(&self) -> u32 {
        self.handshake["app_protocol_version"].as_u64().and_then(|v| u32::try_from(v).ok()).unwrap_or(0)
    }

    /// Issue `GetInfo` and await the app's `AppInfo`, computing protocol skew.
    /// Returns a JSON summary regardless of outcome (never propagates an error, so
    /// connect always succeeds). On a `bridge > app` skew, warns to stderr — the
    /// dangerous direction, where commands the older app lacks are silently ignored.
    fn perform_handshake(&mut self) -> Value {
        let id = self.next_id();
        if let Err(e) = self.send(&protocol::get_info(id)) {
            // Command not even delivered. App not listening yet (0 receivers), or an
            // app is bound but not consuming (blocked). Not fatal; classify + report.
            let skew = if self.cmd_receivers() == 0 { "no_app" } else { "blocked" };
            return json!({
                "ok": false,
                "skew": skew,
                "bridge_build_id": protocol::BRIDGE_BUILD_ID,
                "error": e.to_string(),
            });
        }
        let bridge_ver = protocol::AGENT_PROTOCOL_VERSION;
        let deadline = Instant::now() + OP_TIMEOUT;
        while Instant::now() < deadline {
            let buf = match self.events.recv(Some(RECV_POLL_MS)) {
                Ok(b) => b,
                Err(e) => return json!({ "ok": false, "error": e.to_string() }),
            };
            if buf.is_empty() {
                continue;
            }
            self.record_event(buf.data());
            let (_seq, incoming) = match protocol::parse_event(buf.data()) {
                Ok(v) => v,
                Err(_) => continue,
            };
            match incoming {
                Incoming::AppInfo { request_id, protocol_version, app_version, build_id, instance_id }
                    if request_id == id =>
                {
                    let skew = if protocol_version == bridge_ver {
                        "match"
                    } else if bridge_ver > protocol_version {
                        "app_older"
                    } else {
                        "app_newer"
                    };
                    if skew == "app_older" {
                        eprintln!(
                            "sourcetrail-mcp: protocol skew — app v{protocol_version} < bridge \
                             v{bridge_ver}; commands added after v{protocol_version} will be ignored"
                        );
                    }
                    // Soft two-sided wire check: the handshake round-tripped, so the
                    // thoth-ipc wire is compatible — but a version *difference* still
                    // signals a pending drift worth flagging (e.g. wire-compatible
                    // patch releases now, a breaking bump later). "unknown" until the
                    // app stamps its build_id.
                    let wire = match thoth_token(&build_id) {
                        None => "unknown",
                        Some(app_t) => match thoth_token(protocol::BRIDGE_BUILD_ID) {
                            Some(br_t) if app_t == br_t => "match",
                            Some(_) => "drift",
                            None => "unknown",
                        },
                    };
                    if wire == "drift" {
                        eprintln!(
                            "sourcetrail-mcp: thoth-ipc drift — app {build_id} vs bridge {}; \
                             still wire-compatible now, but realign before the next breaking bump",
                            protocol::BRIDGE_BUILD_ID
                        );
                    }
                    return json!({
                        "ok": true,
                        "bridge_protocol_version": bridge_ver,
                        "app_protocol_version": protocol_version,
                        "app_version": app_version,
                        "app_build_id": build_id,
                        "bridge_build_id": protocol::BRIDGE_BUILD_ID,
                        "instance_id": instance_id,
                        "skew": skew,
                        "wire": wire,
                    });
                }
                // The app rejected GetInfo as unknown → it predates the handshake, so
                // its protocol is older than any version that defines GetInfo.
                Incoming::CommandResult { request_id, ok: false, .. } if request_id == id => {
                    eprintln!(
                        "sourcetrail-mcp: protocol skew — app predates the handshake (GetInfo \
                         unsupported); newer commands may be ignored"
                    );
                    return json!({
                        "ok": true,
                        "bridge_protocol_version": bridge_ver,
                        "app_protocol_version": Value::Null,
                        "skew": "app_older",
                        "note": "app predates the handshake (GetInfo unsupported)",
                    });
                }
                _ => {}
            }
        }
        // Timed out with no AppInfo. If an app receiver IS bound, the app is up and
        // received GetInfo but its reply never came back parseable — the classic
        // signature of a bridge/app version skew (the low-level thoth-ipc sync-ABI
        // still attaches, so the segment connects and recv_count>0, but the message
        // wire no longer round-trips). Call it out loudly with the fix, instead of a
        // bare "timed out" that sends the operator hunting (as it did on 2026-07-18).
        let receivers = self.cmd_receivers();
        if receivers >= 1 {
            eprintln!(
                "sourcetrail-mcp: cannot handshake with the app on {} — {receivers} receiver(s) \
                 bound but no reply. Likely a version skew: this bridge ({}) and the app were \
                 built against different thoth-ipc. Rebuild the bridge: \
                 `cargo build --release --features mcp --bin sourcetrail-mcp`.",
                self.cmd_name, protocol::BRIDGE_BUILD_ID
            );
            return json!({
                "ok": false,
                "skew": "unreachable",
                "bridge_build_id": protocol::BRIDGE_BUILD_ID,
                "cmd_receivers": receivers,
                "error": format!(
                    "app is listening ({receivers} receiver(s)) but did not answer the handshake \
                     within {OP_TIMEOUT:?} — likely a bridge/app thoth-ipc version skew (rebuild \
                     the bridge with `cargo build --release --features mcp`), or the app is blocked \
                     on its GUI thread (e.g. a modal dialog)"
                ),
            });
        }
        json!({
            "ok": false,
            "skew": "no_app",
            "bridge_build_id": protocol::BRIDGE_BUILD_ID,
            "error": "handshake timed out (no AppInfo) and no app receiver is bound — the app is \
                      not running with agent control, or exited before the handshake",
        })
    }

    fn next_id(&mut self) -> u64 {
        let id = self.next_id;
        self.next_id += 1;
        id
    }

    fn send(&mut self, bytes: &[u8]) -> Result<()> {
        // `send` returns Ok(false) when no receiver consumed the frame within the
        // timeout. Surface that as a distinct error instead of proceeding to a
        // misleading reply-wait timeout downstream — and say WHICH failure it is:
        // no app listening vs an app connected but not consuming. The two need
        // different operator responses.
        let delivered = self.cmd.send(bytes, SEND_TIMEOUT_MS).context("send command")?;
        if !delivered {
            if self.cmd_receivers() == 0 {
                bail!(
                    "command not delivered on {}: no app is listening — Sourcetrail is not \
                     running with agent control, or this is a stale channel left by a killed \
                     instance (a live app registers on startup, so a zero count means no app)",
                    self.cmd_name
                );
            }
            bail!(
                "command not delivered on {}: {} app receiver(s) are connected but none consumed \
                 the command within {SEND_TIMEOUT_MS}ms — the app may be blocked (e.g. a modal \
                 dialog holding the GUI thread) or stuck",
                self.cmd_name,
                self.cmd_receivers()
            );
        }
        self.last_sent = protocol::queued_of(bytes);
        if let Some((id, command)) = protocol::action_of(bytes) {
            if self.requests.len() >= REQUEST_LOG_CAP {
                self.requests.pop_front();
            }
            self.requests.push_back(SentRequest { id, command, sent_ms: now_ms() });
        }
        Ok(())
    }

    /// Non-destructive liveness probe: how many app receivers are on the command
    /// channel right now (the bridge is the sender there, so this counts apps, not
    /// itself). 0 ⇒ no app listening (down, or a stale channel a killed instance
    /// left); ≥1 ⇒ an app is connected. Only trustworthy because the app wipes its
    /// channels on startup (see AgentControlController) — otherwise phantom
    /// receiver bits from killed peers would inflate this count.
    fn cmd_receivers(&self) -> usize {
        self.cmd.recv_count()
    }

    /// Uniform "reply never arrived" error. Reaching a reply-wait means the command
    /// was already delivered (send succeeded), so this is the app failing to
    /// *reply* — a different failure than a non-delivered command, and worth
    /// distinguishing "app gone" from "app stuck" with a live liveness check.
    fn reply_timeout(&self, what: &str, request_id: u64, timeout: Duration) -> anyhow::Error {
        let tail = match self.cmd_receivers() {
            0 => "the app is no longer listening — it likely crashed or exited after receiving \
                  the command"
                .to_string(),
            // From v3 the GUI-only commands bypass the message thread, so they still
            // answer when a dialog blocks it: point there.
            n if self.app_protocol_version() >= 3 => format!(
                "{n} app receiver(s) still connected — the app received the command but sent no \
                 reply. A dialog waiting for an answer blocks it: ui_frame shows what is on screen \
                 and ui_query/ui_invoke can answer it, since those still work while it waits"
            ),
            n => format!(
                "{n} app receiver(s) still connected — the app received the command but sent no \
                 reply (it may be blocked on the GUI thread, e.g. a modal dialog, or its reply \
                 raced past this window)"
            ),
        };
        // If the connect-time handshake ALSO failed while the app is bound, this
        // isn't a one-off race — it's systemic, i.e. a bridge/app version skew.
        let skew_hint = if self.cmd_receivers() >= 1
            && self.handshake.get("ok").and_then(Value::as_bool) != Some(true)
        {
            format!(
                " — NOTE: the connect-time handshake also failed, so this is systemic: suspect a \
                 bridge/app thoth-ipc version skew. Rebuild the bridge (`cargo build --release \
                 --features mcp`). bridge_build_id={}",
                protocol::BRIDGE_BUILD_ID
            )
        } else {
            String::new()
        };
        anyhow!("timed out after {timeout:?} awaiting {what} for request {request_id}: {tail}{skew_hint}")
    }

    /// A wait for a reply within `timeout`; `queued` when the reply waits in the
    /// app's queue.
    fn liveness(&self, timeout: Duration, queued: bool) -> Liveness {
        let on = timeout >= PROBE_EVERY && self.app_protocol_version() >= protocol::MACHINES_SINCE;
        let now = Instant::now();
        Liveness {
            on,
            queued,
            deadline: now + if on { timeout.max(BUSY_LIMIT) } else { timeout },
            heard: now,
            probe: None,
        }
    }

    /// Whether the ack of `request_id` waits in the app's queue (true unless it is
    /// the last command sent and that one is answered aside).
    fn ack_queued(&self, request_id: u64) -> bool {
        self.last_sent.map_or(true, |(id, queued)| id != request_id || queued)
    }

    /// Before each read of a wait for `what`: fail once the wait is past its
    /// deadline or a probe has gone unanswered for UNRESPONSIVE, else probe the app
    /// when it has not answered one for PROBE_EVERY.
    fn check_alive(&mut self, alive: &mut Liveness, what: &str, request_id: u64, timeout: Duration) -> Result<()> {
        let now = Instant::now();
        if now >= alive.deadline {
            return Err(if alive.on {
                self.busy_timeout(what, request_id, timeout.max(BUSY_LIMIT))
            } else {
                self.reply_timeout(what, request_id, timeout)
            });
        }
        if !alive.on {
            return Ok(());
        }
        match alive.probe {
            Some((_, sent)) if now - sent >= UNRESPONSIVE => Err(self.unresponsive(what, request_id, now - alive.heard)),
            None if now - alive.heard >= PROBE_EVERY => {
                let id = self.next_id();
                // All of them: the reply refreshes the machine view, and the
                // question machine in it says whether a question holds the reply.
                self.send(&protocol::get_machines(id, ""))?;
                if self.probes.len() >= 16 {
                    self.probes.pop_front();
                }
                self.probes.push_back(id);
                alive.probe = Some((id, now));
                Ok(())
            }
            _ => Ok(()),
        }
    }

    /// Whether `incoming` is the reply to a probe, taken here. Its `MachineStates`
    /// says the app answers; with a question asking, a reply that waits in the
    /// app's queue will not come until the question is answered.
    fn take_probe_reply(
        &mut self,
        alive: &mut Liveness,
        incoming: &Incoming,
        what: &str,
        request_id: u64,
    ) -> Result<bool> {
        match incoming {
            Incoming::CommandResult { request_id: rid, .. } if self.probes.contains(rid) => Ok(true),
            Incoming::MachineStates { request_id: rid, machines } if self.probes.contains(rid) => {
                self.machines.note_states(machines, machine_view::now_ms());
                self.probes.retain(|probe| probe != rid);
                if alive.probe.is_some_and(|(id, _)| id == *rid) {
                    alive.probe = None;
                    alive.heard = Instant::now();
                    let asking = machines.iter().find(|m| m["machine"] == "question" && m["state"] == "asking");
                    if let (true, Some(question)) = (alive.queued, asking) {
                        return Err(anyhow::Error::new(BehindQuestion(format!(
                            "{what} for request {request_id} waits for a question the app is asking: \
                             answer it with machine_send {{ machine: \"question\", event: \"answer\", \
                             args: {{ label: ... }} }} (question: {question})"
                        ))));
                    }
                }
                Ok(true)
            }
            _ => Ok(false),
        }
    }

    /// The next event off st.agent.events for a wait (None when none came within
    /// one poll), recorded for poll_events. Probes the app meanwhile; see Liveness.
    fn next_event(
        &mut self,
        alive: &mut Liveness,
        what: &str,
        request_id: u64,
        timeout: Duration,
    ) -> Result<Option<Incoming>> {
        self.check_alive(alive, what, request_id, timeout)?;
        let buf = self.events.recv(Some(RECV_POLL_MS)).context("recv event")?;
        if buf.is_empty() {
            return Ok(None);
        }
        let incoming = match protocol::parse_event(buf.data()) {
            Ok((_seq, incoming)) => incoming,
            Err(e) => {
                self.record_event(buf.data());
                self.note_dropped_frame("event", &e);
                return Ok(None);
            }
        };
        if self.take_probe_reply(alive, &incoming, what, request_id)? {
            return Ok(None);
        }
        self.record_event(buf.data());
        Ok(Some(incoming))
    }

    /// The probe replies on st.agent.events while a reply is awaited on another
    /// channel; everything else read meanwhile is recorded.
    fn take_probe_replies(&mut self, alive: &mut Liveness, what: &str, request_id: u64) -> Result<()> {
        for _ in 0..64 {
            let buf = match self.events.try_recv() {
                Ok(buf) if !buf.is_empty() => buf,
                _ => break,
            };
            match protocol::parse_event(buf.data()) {
                Ok((_seq, incoming)) if self.take_probe_reply(alive, &incoming, what, request_id)? => {}
                _ => self.record_event(buf.data()),
            }
        }
        Ok(())
    }

    /// The app answered all along, but `what` did not come.
    fn busy_timeout(&self, what: &str, request_id: u64, waited: Duration) -> anyhow::Error {
        self.not_answering(
            format!(
                "{what} for request {request_id} did not come within {waited:?}, though the app kept \
                 answering: its queue is busy with something else (with a debugger attached, \
                 instance_debug_command \"~*k\" shows what)"
            ),
            Duration::ZERO,
        )
    }

    /// The app stopped answering while `what` was awaited, `silent` long.
    fn unresponsive(&self, what: &str, request_id: u64, silent: Duration) -> anyhow::Error {
        if self.cmd_receivers() == 0 {
            return self.not_answering(
                format!("the app went away awaiting {what} for request {request_id}: it likely crashed or exited"),
                silent,
            );
        }
        self.not_answering(
            format!(
                "the app has not answered for {silent:.1?} awaiting {what} for request {request_id}, \
                 not even the state machines its reader thread answers: it is stuck or hung (with a \
                 debugger attached, instance_debug_command \"~*k\" shows where)"
            ),
            silent,
        )
    }

    /// `message`, with what the app's state machines were busy with as the bridge
    /// last saw them: a line in the message, and the whole tree as the report.
    fn not_answering(&self, message: String, silent: Duration) -> anyhow::Error {
        let now = machine_view::now_ms();
        let in_progress = self.machines.summary(now);
        let summary = if self.machines.is_empty() {
            String::new()
        } else if in_progress.is_empty() {
            "; in progress: nothing, by the state machines".to_string()
        } else {
            format!("; in progress: {}", in_progress.join(", "))
        };
        anyhow::Error::new(AppNotAnswering {
            message: format!("{message}{summary}"),
            report: self.machines.report(silent.as_millis() as u64, now),
        })
    }

    /// Record one raw `EventEnvelope` into the observation log (bounded ring).
    /// Called wherever events are read off `st.agent.events`, so nothing consumed
    /// for reply correlation is lost to `poll_events`.
    fn record_event(&mut self, bytes: &[u8]) {
        if let Ok(v) = protocol::event_to_json(bytes) {
            let at_ms = v["timestamp_ms"].as_u64().unwrap_or_else(machine_view::now_ms);
            match v["type"].as_str() {
                Some("MachineTransition") => self.machines.note_transition(&v, at_ms),
                Some("MachineStates") => {
                    if let Some(machines) = v["machines"].as_array() {
                        self.machines.note_states(machines, at_ms);
                    }
                }
                _ => {}
            }
            // A probe's reply that came after its wait ended.
            if let Some(rid) = v["request_id"].as_u64().filter(|rid| self.probes.contains(rid)) {
                if v["type"] == "MachineStates" {
                    self.probes.retain(|probe| *probe != rid);
                }
                return;
            }
            if self.event_log.len() >= EVENT_LOG_CAP {
                self.event_log.pop_front();
            }
            self.event_log.push_back(v);
        }
    }

    /// Read `st.agent.events` until the `CommandResult` for `request_id`, keeping
    /// any `SearchCompleted` seen meanwhile. Returns `(ok, message, matches?)`.
    /// A slow ack is waited for while the app answers (see Liveness).
    fn await_ack(&mut self, request_id: u64, timeout: Duration) -> Result<(bool, String, Option<Value>)> {
        let mut alive = self.liveness(timeout, self.ack_queued(request_id));
        let mut search: Option<Value> = None;
        loop {
            match self.next_event(&mut alive, "CommandResult (ack)", request_id, timeout)? {
                Some(Incoming::CommandResult { request_id: rid, ok, message }) if rid == request_id => {
                    return Ok((ok, message, search));
                }
                Some(Incoming::SearchCompleted(v)) => search = Some(v),
                _ => {}
            }
        }
    }

    /// Read `st.agent.state` until the `UiStateEnvelope` for `request_id`.
    /// A slow one is waited for while the app answers (see Liveness).
    fn read_ui_state(&mut self, request_id: u64, timeout: Duration) -> Result<Value> {
        let mut alive = self.liveness(timeout, true);
        loop {
            self.check_alive(&mut alive, "UiState", request_id, timeout)?;
            if alive.probe.is_some() {
                self.take_probe_replies(&mut alive, "UiState", request_id)?;
            }
            let buf = self.state.recv(Some(RECV_POLL_MS)).context("recv state")?;
            if buf.is_empty() {
                continue;
            }
            // A frame that will not parse is skipped, not fatal. It cannot be
            // this request's reply, and failing here cascaded: the real reply
            // stayed unread, the next request found it first, and every later
            // request ran one frame behind for the life of the connection.
            // Skipping costs a frame; failing cost the session.
            let (rid, json) = match protocol::parse_ui_state(buf.data()) {
                Ok(parsed) => parsed,
                Err(e) => {
                    self.note_dropped_frame("state", &e);
                    continue;
                }
            };
            if let Some(machines) = json["machines"].as_array() {
                self.machines.note_states(machines, machine_view::now_ms());
            }
            if rid == request_id {
                return Ok(json);
            }
        }
    }

    /// Drop any stale `UiSnapshot` frames sitting in the channel (e.g. a reply from
    /// a prior op that timed out and landed late). Called *before* sending a new
    /// snapshot request, so nothing of ours is in flight yet — this can't drop the
    /// current reply, only leftovers. GetSnapshot and QueryUi share this channel.
    fn drain_stale_snapshots(&mut self) {
        for _ in 0..64 {
            match self.snapshot.try_recv() {
                Ok(buf) if !buf.is_empty() => {}	// drop stale frame
                _ => break,
            }
        }
    }

    /// Drop stale `UiStateEnvelope` frames on st.agent.state before asking for a
    /// fresh one. The app publishes UiState only in answer to a GetUiState, so
    /// anything already sitting here is a late reply to a request that gave up —
    /// never ours, since nothing of ours is in flight yet. Same reasoning and
    /// same safety as drain_stale_snapshots.
    fn drain_stale_states(&mut self) {
        for _ in 0..64 {
            match self.state.try_recv() {
                Ok(buf) if !buf.is_empty() => {}	// drop stale frame
                _ => break,
            }
        }
    }

    /// Drop stale frames on st.agent.events before issuing a command whose reply
    /// is an *event*. Unlike the state and snapshot drains these are recorded
    /// rather than discarded, because events are also the poll_events log and a
    /// drain must not create a hole in it.
    ///
    /// This is what makes `search` correlate at all: SearchCompleted carries no
    /// request id (agent_event.fbs), so the loop can only take the first one it
    /// sees. Draining first is what makes "the first one" mean "ours" — without
    /// it, one timed-out search leaves a result behind and every later query
    /// answers with its predecessor's matches.
    fn drain_stale_events(&mut self) {
        for _ in 0..64 {
            match self.events.try_recv() {
                Ok(buf) if !buf.is_empty() => self.record_event(buf.data()),
                _ => break,
            }
        }
    }

    /// Count and log a frame that would not parse. Visible rather than silent:
    /// dropping frames is recovery, not correctness, so a rising count is the
    /// symptom worth chasing.
    fn note_dropped_frame(&mut self, channel: &str, err: &anyhow::Error) {
        self.dropped_frames = self.dropped_frames.saturating_add(1);
        // stderr, not stdout: stdout is the MCP stdio transport.
        eprintln!("sourcetrail-mcp: dropped unparseable {channel} frame #{}: {err}", self.dropped_frames);
    }

    /// Read `st.agent.snapshot` until the `UiSnapshot` for `request_id`.
    fn read_snapshot(&mut self, request_id: u64, timeout: Duration) -> Result<Value> {
        let deadline = Instant::now() + timeout;
        while Instant::now() < deadline {
            let buf = self.snapshot.recv(Some(RECV_POLL_MS)).context("recv snapshot")?;
            if buf.is_empty() {
                continue;
            }
            let (rid, json) = match protocol::parse_ui_snapshot(buf.data()) {
                Ok(parsed) => parsed,
                Err(e) => {
                    self.note_dropped_frame("snapshot", &e);
                    continue;
                }
            };
            if rid == request_id {
                return Ok(json);
            }
        }
        return Err(self.reply_timeout("UiSnapshot", request_id, timeout))
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
            return Err(rejection(label, &msg));
        }
        Ok((id, msg))
    }

    /// Read events (recording all meanwhile) until the `Settled` for `request_id`
    /// — the queue-idle barrier that means the command's synchronous fan-out has
    /// drained, so the next read observes the *settled* state.
    /// The queue is what settles, so a question it waits behind ends the wait.
    fn await_settled(&mut self, request_id: u64, timeout: Duration) -> Result<()> {
        let mut alive = self.liveness(timeout, true);
        loop {
            if let Some(Incoming::Settled { request_id: rid }) =
                self.next_event(&mut alive, "Settled", request_id, timeout)?
            {
                if rid == request_id {
                    return Ok(());
                }
            }
        }
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
            "bridge_build_id": protocol::BRIDGE_BUILD_ID,
            "protocol": self.handshake,
            "dropped_frames": self.dropped_frames,
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
        self.drain_events();
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

    fn drain_events(&mut self) {
        for _ in 0..EVENT_LOG_CAP {
            match self.events.try_recv() {
                Ok(buf) if !buf.is_empty() => self.record_event(buf.data()),
                _ => break,
            }
        }
    }

    /// What the commands this bridge sent led to: for request `request_id`, or the
    /// `last` ones, each with its command, its ack, when it settled, and the events
    /// that followed from it (`caused_by`, protocol v7): activations, the machines'
    /// transitions, a dialog that came up. Times are ms after the command was sent.
    pub fn request_log(&mut self, request_id: Option<u64>, last: usize) -> Value {
        self.drain_events();
        let chosen: Vec<&SentRequest> = match request_id {
            Some(id) => self.requests.iter().filter(|r| r.id == id).collect(),
            None => {
                let skip = self.requests.len().saturating_sub(last.max(1));
                self.requests.iter().skip(skip).collect()
            }
        };
        let requests: Vec<Value> = chosen.iter().map(|r| self.request_entry(r)).collect();
        let mut v = json!({ "requests": requests });
        if self.app_protocol_version() < protocol::CAUSES_SINCE {
            v["note"] = json!(format!(
                "this app speaks agent-control protocol v{} and does not say what follows from a \
                 command (needs v{}): only its ack and Settled are matched",
                self.app_protocol_version(),
                protocol::CAUSES_SINCE
            ));
        }
        v
    }

    fn request_entry(&self, request: &SentRequest) -> Value {
        let after = |e: &Value| {
            e.get("timestamp_ms").and_then(Value::as_u64).map(|t| t as i64 - request.sent_ms as i64)
        };
        let mut ack = Value::Null;
        let mut settled_after_ms = Value::Null;
        let mut events = Vec::new();
        for e in &self.event_log {
            let caused = e.get("caused_by").and_then(Value::as_u64) == Some(request.id);
            let answers = e.get("request_id").and_then(Value::as_u64) == Some(request.id);
            if !caused && !answers {
                continue;
            }
            match e.get("type").and_then(Value::as_str) {
                Some("CommandResult") => {
                    ack = json!({ "ok": e["ok"], "message": e["message"], "after_ms": after(e) });
                }
                Some("Settled") => settled_after_ms = json!(after(e)),
                // Replies to the command itself, not what it led to.
                Some("MachineStates") | Some("AppInfo") => {}
                Some(kind) => {
                    let mut summary = json!({ "seq": e["seq"], "after_ms": after(e), "type": kind });
                    for key in ["machine", "from", "to", "event", "dialog", "file_path", "state"] {
                        if let Some(value) = e.get(key) {
                            summary[key] = value.clone();
                        }
                    }
                    if let Some(nodes) = e.get("nodes").and_then(Value::as_array) {
                        let names: Vec<&Value> =
                            nodes.iter().filter_map(|n| n.get("display_name")).collect();
                        summary["nodes"] = json!(names);
                    }
                    events.push(summary);
                }
                None => {}
            }
        }
        json!({
            "request_id": request.id,
            "command": request.command,
            "sent_ms": request.sent_ms,
            "ack": ack,
            "settled_after_ms": settled_after_ms,
            "events": events,
        })
    }

    pub fn get_ui_state(&mut self) -> Result<Value> {
        self.get_ui_state_with_timeout(OP_TIMEOUT)
    }

    /// `get_ui_state` with an explicit reply window. A short one abandons the
    /// reply in flight, which is the interesting case: the app has already
    /// committed the payload to a chunk, and nothing frees that chunk until
    /// someone reads the frame. There are only 32 chunk slots per size, so
    /// abandoning enough replies exhausts the pool and the app silently falls
    /// back to 64-byte inline fragments — thousands of them for a large reply,
    /// through a 256-slot ring. Hence the drain below: it is what returns those
    /// chunks, and skipping it is what lets a run of timeouts wedge a connection.
    pub fn get_ui_state_with_timeout(&mut self, timeout: Duration) -> Result<Value> {
        self.drain_stale_states();
        let id = self.next_id();
        self.send(&protocol::get_ui_state(id))?;
        self.read_ui_state(id, timeout)
    }

    /// Capture the structural UI tree (accessibility, or the raw object tree). The
    /// app acks on events, then sends the `UiSnapshot` on st.agent.snapshot (the
    /// capture hops to the GUI thread, so allow a generous window).
    pub fn get_snapshot(&mut self, object_tree: bool) -> Result<Value> {
        self.drain_stale_snapshots();
        let (id, _) =
            self.send_and_ack("get_snapshot", OP_TIMEOUT, |id| protocol::get_snapshot(id, object_tree))?;
        self.read_snapshot(id, SNAPSHOT_TIMEOUT)
    }

    /// Search has a two-part reply: a `CommandResult` ack (which may reject, e.g.
    /// no project) and a `SearchCompleted` event carrying the matches. The app runs
    /// the query on its message thread and emits the result *after* `handleCommand`
    /// returns, so `SearchCompleted` can arrive after the ack. Read past the ack
    /// until the matches land, rather than returning on the ack (which raced to an
    /// empty result).
    pub fn search(&mut self, query: &str) -> Result<Value> {
        self.search_with_timeout(query, OP_TIMEOUT)
    }

    /// `search` with an explicit reply window. A short one abandons both the ack
    /// and the (large) SearchCompleted event, which is the case worth testing:
    /// unlike the state channel, events arrive several per request, so abandoned
    /// ones accumulate faster than later requests consume them. Each large one
    /// holds a chunk, and there are only 32 per chunk size.
    pub fn search_with_timeout(&mut self, query: &str, timeout: Duration) -> Result<Value> {
        // SearchCompleted carries no request id, so the loop below can only take
        // the first result it sees. Draining first is what makes "the first" mean
        // "ours" -- without it one timed-out search leaves its result queued and
        // every later query answers with its predecessor's matches.
        self.drain_stale_events();
        let id = self.next_id();
        self.send(&protocol::search(id, query))?;

        let mut alive = self.liveness(timeout, true);
        let mut acked = false;
        loop {
            let incoming = match self.next_event(&mut alive, "search reply", id, timeout) {
                Ok(incoming) => incoming,
                Err(e) if acked => return Err(e.context("search accepted but its results did not come")),
                Err(e) => return Err(e),
            };
            match incoming {
                Some(Incoming::SearchCompleted(v)) => return Ok(v),
                Some(Incoming::CommandResult { request_id: rid, ok, message }) if rid == id => {
                    if !ok {
                        return Err(rejection("search", &message));
                    }
                    acked = true; // accepted — the SearchCompleted event is on its way
                }
                _ => {}
            }
        }
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

    pub fn scroll_to_line(&mut self, path: &str, line: u32) -> Result<Value> {
        self.act_and_observe("scroll_to_line", |id| protocol::scroll_to_line(id, path, line))
    }

    /// The graph's controls (protocol v7), each returning the settled ui_state.
    pub fn expand_node(&mut self, node_id: u64, expand: bool) -> Result<Value> {
        self.require_graph()?;
        self.act_and_observe("graph_expand", |id| protocol::expand_node(id, node_id, expand))
    }

    pub fn hide_node(&mut self, node_id: u64) -> Result<Value> {
        self.require_graph()?;
        self.act_and_observe("graph_hide", |id| protocol::hide_node(id, node_id))
    }

    pub fn split_bundle(&mut self, bundle_id: u64) -> Result<Value> {
        self.require_graph()?;
        self.act_and_observe("graph_split", |id| protocol::split_bundle(id, bundle_id))
    }

    pub fn activate_trail(&mut self, trail: &protocol::Trail) -> Result<Value> {
        self.require_graph()?;
        self.act_and_observe("graph_trail", |id| protocol::activate_trail(id, trail))
    }

    fn require_graph(&self) -> Result<()> {
        let app_version = self.app_protocol_version();
        if app_version < protocol::GRAPH_SINCE {
            bail!(
                "this app speaks agent-control protocol v{app_version} and has no graph commands \
                 (needs v{}): rebuild the app",
                protocol::GRAPH_SINCE
            );
        }
        Ok(())
    }

    /// Back (`forward == false`) or forward in the navigation history.
    pub fn history(&mut self, forward: bool) -> Result<Value> {
        let label = if forward { "history forward" } else { "history back" };
        self.act_and_observe(label, |id| protocol::history(id, forward))
    }

    /// Structural control: invoke an accessible `action` (or set `text`) on the
    /// element addressed by a snapshot `ref` (object_name + role/name/index path).
    /// The app resolves + invokes on the GUI thread; the ack **is** the result, and
    /// resolution failures ("element not found", "action not supported") come back
    /// as data (`{ ok, message }`), not transport errors. An action that started an
    /// activation (Back, Home, a search result) returns once the views have drawn
    /// it, with `drawn` (false: not within the timeout).
    pub fn invoke_action(
        &mut self,
        object_name: &str,
        path: &[(String, String, u32)],
        action: &str,
        text: &str,
        expect_hash: u64,
    ) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::invoke_action(id, object_name, path, action, text, expect_hash))?;
        let (ok, message, _) = self.await_ack(id, OP_TIMEOUT)?;
        if ok {
            if let Some(drawn) = self.await_activation(OP_TIMEOUT) {
                return Ok(json!({ "ok": ok, "message": message, "drawn": drawn }));
            }
        }
        Ok(json!({ "ok": ok, "message": message }))
    }

    /// Wait for the activation machine to be idle: the graph and code views have
    /// drawn what the last activation gave them. An action that dispatched one finds
    /// it going already. None when there was nothing to wait for (or the app has no
    /// activation machine), else whether it went idle within `timeout`. Re-reads the
    /// machine now and then: a transition between its snapshot and the wait is not
    /// lost on it.
    fn await_activation(&mut self, timeout: Duration) -> Option<bool> {
        if self.app_protocol_version() < protocol::MACHINES_SINCE {
            return None;
        }
        let deadline = Instant::now() + timeout;
        let idle = ["idle".to_string()];
        let mut waited = false;
        loop {
            let state = self.machine_snapshot("activation").ok()?["state"].as_str()?.to_string();
            if state == "idle" {
                return waited.then_some(true);
            }
            waited = true;
            let now = Instant::now();
            if now >= deadline {
                return Some(false);
            }
            let slice = (deadline - now).min(Duration::from_millis(500));
            let mut transitions = Vec::new();
            if self.await_machine_state("activation", &idle, state, slice, &mut transitions).ok()? {
                return Some(true);
            }
        }
    }

    /// The screen as a user sees it: every visible top-level window composited at
    /// its position, downscaled to `max_width` pixels when non-zero, cropped first
    /// to `region` (`[x, y, w, h]` in the frame's own pixels) when given. Returns
    /// `{ ok, frame, windows }` with the PNG base64-encoded, or
    /// `{ ok: false, message }`.
    pub fn get_frame(&mut self, max_width: u32, region: Option<[i32; 4]>) -> Result<Value> {
        // An app before v2 has the GetFrame arm in its schema but answers it as an
        // unknown command; say what to do about it instead.
        let app_version = self.app_protocol_version();
        if app_version < protocol::GET_FRAME_SINCE {
            bail!(
                "this app speaks agent-control protocol v{app_version} and cannot capture the screen \
                 (needs v{}): rebuild the app",
                protocol::GET_FRAME_SINCE
            );
        }
        let id = self.next_id();
        self.send(&protocol::get_frame(id, max_width, region))?;
        let (ok, message, _) = self.await_ack(id, OP_TIMEOUT)?;
        if !ok {
            return Ok(json!({ "ok": false, "message": message }));
        }
        let frame = self.read_frame(id, OP_TIMEOUT)?;
        // The ack names the composited windows ("windows: 'title' WxH (modal); ..."),
        // so a dialog over the main window shows in the result, not only in pixels.
        // From v5 it ends with " | origin X,Y scale S": where the frame sits on screen.
        let (windows, geometry) = match message.split_once(" | origin ") {
            Some((windows, geometry)) => (windows, parse_frame_geometry(geometry)),
            None => (message.as_str(), None),
        };
        let windows = windows.strip_prefix("windows: ").unwrap_or(windows).to_string();
        let mut v = json!({ "ok": true, "frame": frame, "windows": windows });
        if let Some((x, y, scale)) = geometry {
            self.last_frame = Some((x, y, scale));
            v["frame"]["origin"] = json!({ "x": x, "y": y });
            v["frame"]["scale"] = json!(scale);
        }
        Ok(v)
    }

    /// The global point a pixel of the last `get_frame` shows.
    pub fn frame_to_global(&self, px: f64, py: f64) -> Result<(i32, i32)> {
        let (x0, y0, scale) = self.last_frame.ok_or_else(|| {
            anyhow!("no frame to map from: take one with ui_frame first (an app on agent-control protocol v5+)")
        })?;
        Ok(((x0 + px / scale).round() as i32, (y0 + py / scale).round() as i32))
    }

    /// Deliver a gesture of synthetic input events, then wait until their effects
    /// drained and read the state they led to. Returns `{ ok, message, settled,
    /// state }`; a target that does not resolve is `{ ok: false, message }`. When the
    /// effects do not drain in time (a click opened a dialog that holds the app),
    /// `settled` is false and there is no state.
    pub fn send_input(&mut self, target: &protocol::InputTarget, events: &[protocol::InputEvent]) -> Result<Value> {
        let app_version = self.app_protocol_version();
        if app_version < protocol::SEND_INPUT_SINCE {
            bail!(
                "this app speaks agent-control protocol v{app_version} and cannot take synthetic input \
                 (needs v{}): rebuild the app",
                protocol::SEND_INPUT_SINCE
            );
        }
        let id = self.next_id();
        self.send(&protocol::send_input(id, target, events))?;
        let (ok, message, _) = self.await_ack(id, OP_TIMEOUT)?;
        if !ok {
            return Ok(json!({ "ok": false, "message": message }));
        }
        if let Err(e) = self.await_settled(id, OP_TIMEOUT) {
            return Ok(json!({ "ok": true, "message": message, "settled": false, "note": format!("{e:#}") }));
        }
        let state = self.get_ui_state()?;
        Ok(json!({ "ok": true, "message": message, "settled": true, "state": state }))
    }

    /// The state machines (protocol v6): all of them, or the one `machine` names.
    /// Returns `{ machines: [ { machine, state, events, context, revision } ] }`.
    pub fn get_machines(&mut self, machine: &str) -> Result<Value> {
        self.require_machines()?;
        self.drain_stale_events();
        let id = self.next_id();
        self.send(&protocol::get_machines(id, machine))?;
        let reply = self.await_machine_reply(id, machine, UNRESPONSIVE)?;
        if !reply.ok {
            return Err(rejection("machine_state", &reply.message));
        }
        Ok(json!({ "machines": reply.machines }))
    }

    /// Send `machine` its `event` (args: a JSON object, "" for none), as a
    /// dialog's button would. With `until`, then wait up to `timeout` for the
    /// machine to reach one of those states. Returns `{ ok, message, machine,
    /// transitions }` (the machine as it is after the event, or once reached), plus
    /// `reached` with `until`. A refused event is `ok: false` with the machine's
    /// state and accepted events, not an error.
    pub fn send_machine_event(
        &mut self,
        machine: &str,
        event: &str,
        args: &str,
        until: &[String],
        timeout: Duration,
    ) -> Result<Value> {
        self.require_machines()?;
        self.drain_stale_events();
        let id = self.next_id();
        self.send(&protocol::send_machine_event(id, machine, event, args))?;
        let reply = self.await_machine_reply(id, machine, UNRESPONSIVE)?;
        let snapshot = reply.machines.into_iter().next().unwrap_or(Value::Null);
        let mut transitions = reply.transitions;
        if !reply.ok || until.is_empty() {
            return Ok(json!({
                "ok": reply.ok,
                "message": reply.message,
                "machine": snapshot,
                "transitions": transitions,
            }));
        }
        let deadline = Instant::now() + timeout;
        let current = snapshot["state"].as_str().unwrap_or_default().to_string();
        let mut reached = self.await_machine_state(machine, until, current, timeout, &mut transitions)?;
        let snapshot = self.machine_snapshot(machine)?;
        if reached {
            reached = self.await_reopened_project(machine, &snapshot, deadline, &mut transitions)?;
        }
        Ok(json!({
            "ok": true,
            "message": reply.message,
            "machine": snapshot,
            "transitions": transitions,
            "reached": reached,
        }))
    }

    /// Wait up to `timeout` for `machine` to be in one of `until` (sending
    /// nothing). Returns `{ machine, transitions, reached }`.
    pub fn wait_machine(&mut self, machine: &str, until: &[String], timeout: Duration) -> Result<Value> {
        let deadline = Instant::now() + timeout;
        let mut snapshot = self.machine_snapshot(machine)?;
        let current = snapshot["state"].as_str().unwrap_or_default().to_string();
        let mut transitions = Vec::new();
        let mut reached = until.contains(&current);
        if !reached {
            reached = self.await_machine_state(machine, until, current, timeout, &mut transitions)?;
            snapshot = self.machine_snapshot(machine)?;
        }
        if reached {
            reached = self.await_reopened_project(machine, &snapshot, deadline, &mut transitions)?;
        }
        Ok(json!({ "machine": snapshot, "transitions": transitions, "reached": reached }))
    }

    /// An indexing run reopens the project on its new index, and asks for that
    /// before it ends. So a wait that saw the run end (the indexing machine idle)
    /// also waits, until `deadline`, for the reopening to end: ready again, or
    /// no_project when it failed. The caller can navigate at once instead of
    /// hitting "still loading". The project's moves join `transitions`. False when
    /// the deadline passed first.
    fn await_reopened_project(
        &mut self,
        machine: &str,
        snapshot: &Value,
        deadline: Instant,
        transitions: &mut Vec<Value>,
    ) -> Result<bool> {
        if machine != "indexing" || snapshot["state"] != "idle" {
            return Ok(true);
        }
        let project = self.machine_snapshot("project")?;
        let current = project["state"].as_str().unwrap_or_default().to_string();
        if current != "loading" && current != "opening" {
            return Ok(true);
        }
        let settled = ["ready".to_string(), "no_project".to_string()];
        let remaining = deadline.saturating_duration_since(Instant::now());
        self.await_machine_state("project", &settled, current, remaining, transitions)
    }

    fn machine_snapshot(&mut self, machine: &str) -> Result<Value> {
        let machines = self.get_machines(machine)?;
        Ok(machines["machines"].get(0).cloned().unwrap_or(Value::Null))
    }

    fn require_machines(&self) -> Result<()> {
        let app_version = self.app_protocol_version();
        if app_version < protocol::MACHINES_SINCE {
            bail!(
                "this app speaks agent-control protocol v{app_version} and has no state machines \
                 (needs v{}): rebuild the app",
                protocol::MACHINES_SINCE
            );
        }
        Ok(())
    }

    /// Read events until the ack and the `MachineStates` of request `id` (the app
    /// sends both, the ack first), keeping `machine`'s transitions seen meanwhile:
    /// those the event itself caused arrive before the ack. The app's reader thread
    /// answers these, whatever else it is busy with, so no answer within `timeout`
    /// means it does not answer at all.
    fn await_machine_reply(&mut self, id: u64, machine: &str, timeout: Duration) -> Result<MachineReply> {
        let deadline = Instant::now() + timeout;
        let mut ack: Option<(bool, String)> = None;
        let mut transitions = Vec::new();
        while Instant::now() < deadline {
            let buf = self.events.recv(Some(RECV_POLL_MS)).context("recv event")?;
            if buf.is_empty() {
                continue;
            }
            self.record_event(buf.data());
            let incoming = match protocol::parse_event(buf.data()) {
                Ok((_seq, incoming)) => incoming,
                Err(e) => {
                    self.note_dropped_frame("event", &e);
                    continue;
                }
            };
            match incoming {
                Incoming::CommandResult { request_id, ok, message } if request_id == id => ack = Some((ok, message)),
                Incoming::MachineTransition(t) if machine.is_empty() || t["machine"] == machine => {
                    transitions.push(t)
                }
                Incoming::MachineStates { request_id, machines } if request_id == id => {
                    let (ok, message) = ack.unwrap_or((false, "no acknowledgement".to_string()));
                    return Ok(MachineReply { ok, message, machines, transitions });
                }
                _ => {}
            }
        }
        Err(self.unresponsive("MachineStates", id, timeout))
    }

    /// Follow `machine`'s transitions from state `current` until it is in one of
    /// `until`. False when `timeout` passed first (not an error: the caller
    /// reports where the machine is).
    fn await_machine_state(
        &mut self,
        machine: &str,
        until: &[String],
        mut current: String,
        timeout: Duration,
        transitions: &mut Vec<Value>,
    ) -> Result<bool> {
        let deadline = Instant::now() + timeout;
        while !until.contains(&current) {
            if Instant::now() >= deadline {
                return Ok(false);
            }
            let buf = self.events.recv(Some(RECV_POLL_MS)).context("recv event")?;
            if buf.is_empty() {
                continue;
            }
            self.record_event(buf.data());
            if let Ok((_seq, Incoming::MachineTransition(t))) = protocol::parse_event(buf.data()) {
                if t["machine"] == machine {
                    current = t["to"].as_str().unwrap_or_default().to_string();
                    transitions.push(t);
                } else if t["machine"] == "question" && t["to"] == "asking" {
                    // The app asks first: nothing moves until it is answered, so the
                    // wait ends here, with the question among the transitions.
                    transitions.push(t);
                    return Ok(false);
                }
            }
        }
        Ok(true)
    }

    /// Screenshot a single element by its ref. The app grabs it on the GUI thread,
    /// acks, then sends a `FrameEnvelope` on st.agent.frames. Returns `{ ok, frame }`
    /// with the PNG base64-encoded, or `{ ok: false, message }` if the element
    /// couldn't be resolved/grabbed.
    pub fn capture_element(
        &mut self,
        object_name: &str,
        path: &[(String, String, u32)],
        include_properties: bool,
        expect_hash: u64,
    ) -> Result<Value> {
        let id = self.next_id();
        self.send(&protocol::capture_element(id, object_name, path, include_properties, expect_hash))?;
        let (ok, message, _) = self.await_ack(id, OP_TIMEOUT)?;
        if !ok {
            return Ok(json!({ "ok": false, "message": message }));
        }
        let frame = self.read_frame(id, OP_TIMEOUT)?;
        Ok(json!({ "ok": true, "frame": frame }))
    }

    /// Configure log forwarding: `qt_rules` drives Qt's `QLoggingCategory` filter
    /// (emission control), `event_pattern` is a regex gating which captured lines
    /// raise a `LogEvent`, `min_level` ("info"/"warning"/"error") is the floor.
    /// Enables forwarding; observe the `LogEvent`s via `poll_events`.
    pub fn set_log_filter(&mut self, qt_rules: &str, event_pattern: &str, min_level: &str) -> Result<Value> {
        let (_, msg) = self.send_and_ack("set_log_filter", OP_TIMEOUT, |id| {
            protocol::set_log_filter(id, qt_rules, event_pattern, min_level)
        })?;
        Ok(json!({ "ok": true, "message": msg }))
    }

    /// Server-side element selection: evaluate `jsonpath` over the UI snapshot in
    /// the app (qt-json-query) and get back a `UiSnapshot` whose roots are the
    /// matched elements — the whole tree never crosses the wire. A bad JSONPath is
    /// surfaced by the ack (send_and_ack bails with the message).
    pub fn query_ui(&mut self, jsonpath: &str) -> Result<Value> {
        self.drain_stale_snapshots();
        let (id, _) = self.send_and_ack("query_ui", OP_TIMEOUT, |id| protocol::query_ui(id, jsonpath))?;
        self.read_snapshot(id, SNAPSHOT_TIMEOUT)
    }

    /// Read st.agent.frames until the `FrameEnvelope` for `request_id`.
    fn read_frame(&mut self, request_id: u64, timeout: Duration) -> Result<Value> {
        let deadline = Instant::now() + timeout;
        while Instant::now() < deadline {
            let buf = self.frames.recv(Some(RECV_POLL_MS)).context("recv frame")?;
            if buf.is_empty() {
                continue;
            }
            let (rid, json) = protocol::parse_frame(buf.data())?;
            if rid == request_id {
                return Ok(json);
            }
        }
        return Err(self.reply_timeout("frame", request_id, timeout))
    }

    /// Load a project and return the `UiState` once the load is done: the app
    /// reports `Loading` from the request until the project is open and the tab the
    /// load opens has its first activation, so the state returned is one the
    /// caller can navigate from. A load that finds no index ends at Ready with the
    /// indexing dialog in `dialog`. A load that asks a question first ("the index
    /// is outdated: reindex?") holds the app until it is answered; from protocol v6
    /// that returns at once as `{ app_state: "Loading", question, note }` rather
    /// than waiting it out. An app before protocol v4 does not report the load, so
    /// its state is returned right after the ack.
    pub fn load_project(&mut self, path: &str) -> Result<Value> {
        self.send_and_ack("load_project", OP_TIMEOUT, |id| protocol::load_project(id, path))?;
        if self.app_protocol_version() < protocol::LOAD_STATE_SINCE {
            return self.get_ui_state();
        }
        let questions = self.app_protocol_version() >= protocol::MACHINES_SINCE;
        let deadline = Instant::now() + LOAD_TIMEOUT;
        loop {
            // The question machine answers on the reader thread, even while the
            // question holds the thread a UiState is read on.
            if questions {
                let question = self.machine_snapshot("question")?;
                if question["state"] == "asking" {
                    return Ok(json!({
                        "app_state": "Loading",
                        "question": question,
                        "note": "the load waits for this answer: machine_send { machine: \"question\", \
                                 event: \"answer\", args: { label: ... } }, then project_open is done \
                                 once ui_state.app_state is no longer Loading",
                    }));
                }
            }
            match self.get_ui_state() {
                Ok(state) if state["app_state"] != "Loading" => return Ok(state),
                Ok(_) => {}
                // A question that came up meanwhile holds the read: look again.
                Err(e) if e.is::<BehindQuestion>() => {}
                Err(e) => return Err(e),
            }
            if Instant::now() >= deadline {
                bail!("the project was still loading after {LOAD_TIMEOUT:?}; app_state follows it");
            }
            std::thread::sleep(LOAD_POLL);
        }
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

#[cfg(test)]
mod tests {
    use super::{parse_frame_geometry, rejection};

    #[test]
    fn frame_geometry_parses() {
        assert_eq!(parse_frame_geometry("189,175 scale 1"), Some((189.0, 175.0, 1.0)));
        assert_eq!(parse_frame_geometry("-10,4 scale 0.571429"), Some((-10.0, 4.0, 0.571429)));
        assert_eq!(parse_frame_geometry("189,175"), None);
        assert_eq!(parse_frame_geometry("1,2 scale 0"), None);
    }

    #[test]
    fn a_rejection_says_rejected_once() {
        assert_eq!(rejection("activate_node", "rejected: no project loaded").to_string(),
                   "activate_node rejected: no project loaded");
        assert_eq!(rejection("search", "unknown command").to_string(), "search rejected: unknown command");
    }
}
