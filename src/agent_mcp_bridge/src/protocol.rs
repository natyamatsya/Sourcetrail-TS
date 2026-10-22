//! Wire protocol: channel names, `Command` builders, and envelope decoders
//! (FlatBuffers ⇄ `serde_json`). The single place that touches the generated
//! types; the rest of the bridge works in `serde_json::Value`.

use anyhow::{anyhow, Result};
use flatbuffers::{FlatBufferBuilder, UnionWIPOffset, WIPOffset};
use serde_json::{json, Value};

// All agent-control types share one generated module (see build.rs / lib.rs): the
// schemas are merged into one self-contained schema, so `Sourcetrail.Agent` maps
// to a single `schema::sourcetrail::agent`.
use crate::schema::sourcetrail::agent as fb;

/// thoth-ipc channel names, namespaced by instance id (must match the C++
/// AgentControlController::agentChannel): empty id -> `st.agent.<base>`, otherwise
/// `st.agent.<id>.<base>`.
pub mod channel {
    fn name(base: &str, instance: &str) -> String {
        if instance.is_empty() {
            format!("st.agent.{base}")
        } else {
            format!("st.agent.{instance}.{base}")
        }
    }
    pub fn cmd(instance: &str) -> String {
        name("cmd", instance)
    }
    pub fn events(instance: &str) -> String {
        name("events", instance)
    }
    pub fn state(instance: &str) -> String {
        name("state", instance)
    }
    pub fn frames(instance: &str) -> String {
        name("frames", instance)
    }
    pub fn snapshot(instance: &str) -> String {
        name("snapshot", instance)
    }
}

// --- Command builders -------------------------------------------------------

fn finish(
    mut b: FlatBufferBuilder,
    request_id: u64,
    command_type: fb::Command,
    command: WIPOffset<UnionWIPOffset>,
) -> Vec<u8> {
    let env = fb::CommandEnvelope::create(
        &mut b,
        &fb::CommandEnvelopeArgs {
            request_id,
            command_type,
            command: Some(command),
        },
    );
    b.finish(env, None);
    b.finished_data().to_vec()
}

/// This bridge's agent-control protocol version, compiled in from the shared schema
/// (`ProtocolVersion.Current`). Compared against the app's value in the handshake
/// (`GetInfo` -> `AppInfo`) to detect skew — a difference means the two were built
/// from different checkouts. See DESIGN_AGENT_UI_CONTROL.md (Protocol handshake).
pub const AGENT_PROTOCOL_VERSION: u32 = fb::ProtocolVersion::Current.0;

/// This bridge binary's build identity (`git:<hash>+thoth-ipc:<version>`, stamped
/// by build.rs). The thoth-ipc version is the SHM wire the app must also speak, so
/// a mismatch here is the stale-binary drift that silently breaks driving.
pub const BRIDGE_BUILD_ID: &str = env!("AGENT_BRIDGE_BUILD_ID");

pub fn get_ui_state(request_id: u64) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let c = fb::GetUiState::create(&mut b, &fb::GetUiStateArgs {});
    finish(b, request_id, fb::Command::GetUiState, c.as_union_value())
}

/// Handshake request. The app replies with an `AppInfo` event (correlated by
/// request_id) carrying its protocol version + build identity.
pub fn get_info(request_id: u64) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let c = fb::GetInfo::create(&mut b, &fb::GetInfoArgs {});
    finish(b, request_id, fb::Command::GetInfo, c.as_union_value())
}

pub fn get_snapshot(request_id: u64, object_tree: bool) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let format = if object_tree {
        fb::SnapshotFormat::ObjectTree
    } else {
        fb::SnapshotFormat::Accessibility
    };
    let c = fb::GetSnapshot::create(&mut b, &fb::GetSnapshotArgs { format });
    finish(b, request_id, fb::Command::GetSnapshot, c.as_union_value())
}

pub fn search(request_id: u64, query: &str) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let q = b.create_string(query);
    let c = fb::Search::create(&mut b, &fb::SearchArgs { query: Some(q) });
    finish(b, request_id, fb::Command::Search, c.as_union_value())
}

pub fn activate_node(request_id: u64, serialized_name: Option<&str>, node_id: u64) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let sn = serialized_name.map(|s| b.create_string(s));
    let c = fb::ActivateNode::create(
        &mut b,
        &fb::ActivateNodeArgs { serialized_name: sn, node_id },
    );
    finish(b, request_id, fb::Command::ActivateNode, c.as_union_value())
}

pub fn activate_file(request_id: u64, file_path: &str) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let p = b.create_string(file_path);
    let c = fb::ActivateFile::create(&mut b, &fb::ActivateFileArgs { file_path: Some(p) });
    finish(b, request_id, fb::Command::ActivateFile, c.as_union_value())
}

pub fn activate_tab(request_id: u64, tab_id: u64) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let c = fb::ActivateTab::create(&mut b, &fb::ActivateTabArgs { tab_id });
    finish(b, request_id, fb::Command::ActivateTab, c.as_union_value())
}

/// The protocol version from which the app answers `GetFrame` (with `region`).
pub const GET_FRAME_SINCE: u32 = 2;

/// The protocol version from which the app reports `Loading` until a load is done.
pub const LOAD_STATE_SINCE: u32 = 4;

/// The protocol version from which the app answers `SendInput`.
pub const SEND_INPUT_SINCE: u32 = 5;

/// The protocol version from which the app has state machines (`GetMachines`,
/// `SendMachineEvent`, `MachineTransition`, `UiState.machines`).
pub const MACHINES_SINCE: u32 = 6;

/// The protocol version from which the app reports the graph as drawn
/// (`UiState.view_graph`) and takes the graph commands (expand, hide, split, trail).
pub const GRAPH_SINCE: u32 = 7;

/// The protocol version from which every event says which request it follows from
/// (`EventEnvelope.caused_by`), and a dialog coming or going is an event.
pub const CAUSES_SINCE: u32 = 7;

/// The request id of a CommandEnvelope and its command's schema name ("ActivateNode").
pub fn command_of(bytes: &[u8]) -> Option<(u64, &'static str)> {
    let envelope = flatbuffers::root::<fb::CommandEnvelope>(bytes).ok()?;
    Some((envelope.request_id(), envelope.command_type().variant_name()?))
}

/// Expand (or collapse) a node's members in the graph.
pub fn expand_node(request_id: u64, node_id: u64, expand: bool) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let c = fb::ExpandNode::create(&mut b, &fb::ExpandNodeArgs { node_id, expand });
    finish(b, request_id, fb::Command::ExpandNode, c.as_union_value())
}

/// Take a node (or an edge) out of the graph.
pub fn hide_node(request_id: u64, node_id: u64) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let c = fb::HideNode::create(&mut b, &fb::HideNodeArgs { node_id });
    finish(b, request_id, fb::Command::HideNode, c.as_union_value())
}

/// Split a bundle or group of the graph into its nodes.
pub fn split_bundle(request_id: u64, bundle_id: u64) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let c = fb::SplitBundle::create(&mut b, &fb::SplitBundleArgs { bundle_id });
    finish(b, request_id, fb::Command::SplitBundle, c.as_union_value())
}

/// What `activate_trail` shows: the trail from or to `node_id` (to `target_id`
/// when not 0), over `edge_kinds` and `node_kinds` (empty: the defaults).
pub struct Trail<'a> {
    pub node_id: u64,
    pub incoming: bool,
    pub target_id: u64,
    pub edge_kinds: &'a [String],
    pub node_kinds: &'a [String],
    pub depth: u32,
    /// None: as the trail buttons lay it out; Some(true): horizontal.
    pub horizontal: Option<bool>,
    pub include_non_indexed: bool,
}

pub fn activate_trail(request_id: u64, trail: &Trail) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let edge_kinds = string_vector(&mut b, trail.edge_kinds);
    let node_kinds = string_vector(&mut b, trail.node_kinds);
    let c = fb::ActivateTrail::create(
        &mut b,
        &fb::ActivateTrailArgs {
            node_id: trail.node_id,
            direction: if trail.incoming { fb::TrailDirection::Incoming } else { fb::TrailDirection::Outgoing },
            target_id: trail.target_id,
            edge_kinds: Some(edge_kinds),
            node_kinds: Some(node_kinds),
            depth: trail.depth,
            layout: match trail.horizontal {
                None => fb::TrailLayout::Auto,
                Some(true) => fb::TrailLayout::Horizontal,
                Some(false) => fb::TrailLayout::Vertical,
            },
            include_non_indexed: trail.include_non_indexed,
        },
    );
    finish(b, request_id, fb::Command::ActivateTrail, c.as_union_value())
}

fn string_vector<'b>(
    b: &mut FlatBufferBuilder<'b>,
    names: &[String],
) -> flatbuffers::WIPOffset<flatbuffers::Vector<'b, flatbuffers::ForwardsUOffset<&'b str>>> {
    let names: Vec<_> = names.iter().map(|n| b.create_string(n)).collect();
    b.create_vector(&names)
}

/// A node of the graph as drawn, with only what applies to its kind.
fn graph_view_node_json(n: fb::GraphViewNode) -> Value {
    let kind = match n.kind() {
        fb::GraphViewNodeKind::Bundle => "bundle",
        fb::GraphViewNodeKind::Group => "group",
        _ => "node",
    };
    let mut v = json!({ "kind": kind, "id": n.id(), "name": n.name().unwrap_or_default(), "node_kind": n.node_kind() });
    if n.parent() >= 0 {
        v["parent"] = json!(n.parent());
    }
    if n.active() {
        v["active"] = json!(true);
    }
    if n.expandable() {
        v["expanded"] = json!(n.expanded());
        v["hidden_members"] = json!(n.hidden_members());
    }
    if kind != "node" {
        v["bundled"] = json!(n.bundled());
        v["splittable"] = json!(n.splittable());
    }
    if let Some(group) = n.group().filter(|g| !g.is_empty()) {
        v["group"] = json!(group);
    }
    v
}

/// Read the state machines: all of them, or the one `machine` names.
pub fn get_machines(request_id: u64, machine: &str) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let m = (!machine.is_empty()).then(|| b.create_string(machine));
    let c = fb::GetMachines::create(&mut b, &fb::GetMachinesArgs { machine: m });
    finish(b, request_id, fb::Command::GetMachines, c.as_union_value())
}

/// Send `machine` its `event`, with `args` a JSON object ("" for none).
pub fn send_machine_event(request_id: u64, machine: &str, event: &str, args: &str) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let m = b.create_string(machine);
    let e = b.create_string(event);
    let a = b.create_string(args);
    let c = fb::SendMachineEvent::create(
        &mut b,
        &fb::SendMachineEventArgs { machine: Some(m), event: Some(e), args: Some(a) },
    );
    finish(b, request_id, fb::Command::SendMachineEvent, c.as_union_value())
}

/// One synthetic input event (see agent_command.fbs, InputEvent).
#[derive(Debug, Clone, Default, PartialEq)]
pub struct InputEvent {
    pub kind: InputKind,
    pub x: i32,
    pub y: i32,
    pub centered: bool,
    /// 0 none, 1 left, 2 right, 3 middle.
    pub button: u8,
    /// Held buttons: bit0 left, bit1 right, bit2 middle.
    pub buttons: u32,
    /// bit0 Shift, bit1 Ctrl, bit2 Alt, bit3 Meta.
    pub modifiers: u32,
    pub wheel_delta: i32,
    /// A Qt::Key code.
    pub key: u32,
    pub text: String,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum InputKind {
    #[default]
    Move,
    Press,
    Release,
    DoubleClick,
    Wheel,
    KeyPress,
    KeyRelease,
}

impl InputKind {
    /// The kind a `ui_input` event names: move, press, release, doubleclick,
    /// wheel, keypress, keyrelease.
    pub fn parse(name: &str) -> Option<Self> {
        Some(match name.trim().to_ascii_lowercase().as_str() {
            "move" => Self::Move,
            "press" => Self::Press,
            "release" => Self::Release,
            "doubleclick" | "double_click" => Self::DoubleClick,
            "wheel" => Self::Wheel,
            "keypress" | "key_press" => Self::KeyPress,
            "keyrelease" | "key_release" => Self::KeyRelease,
            _ => return None,
        })
    }

    fn fb(self) -> fb::InputEventKind {
        match self {
            Self::Move => fb::InputEventKind::MouseMove,
            Self::Press => fb::InputEventKind::MousePress,
            Self::Release => fb::InputEventKind::MouseRelease,
            Self::DoubleClick => fb::InputEventKind::MouseDoubleClick,
            Self::Wheel => fb::InputEventKind::Wheel,
            Self::KeyPress => fb::InputEventKind::KeyPress,
            Self::KeyRelease => fb::InputEventKind::KeyRelease,
        }
    }

    pub fn is_mouse(self) -> bool {
        !matches!(self, Self::KeyPress | Self::KeyRelease)
    }
}

/// Where a gesture goes: the element a ref names, or the widget under a global point.
pub enum InputTarget<'a> {
    Ref { object_name: &'a str, path: &'a [(String, String, u32)] },
    Point { x: i32, y: i32 },
}

pub fn send_input(request_id: u64, target: &InputTarget, events: &[InputEvent]) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let built: Vec<_> = events
        .iter()
        .map(|e| {
            let text = b.create_string(&e.text);
            fb::InputEvent::create(
                &mut b,
                &fb::InputEventArgs {
                    kind: e.kind.fb(),
                    x: e.x,
                    y: e.y,
                    centered: e.centered,
                    button: fb::MouseButton(e.button as i8),
                    buttons: e.buttons,
                    modifiers: e.modifiers,
                    wheel_delta: e.wheel_delta,
                    key: e.key,
                    text: Some(text),
                },
            )
        })
        .collect();
    let events = b.create_vector(&built);
    let (target, use_target_point, target_x, target_y) = match target {
        InputTarget::Ref { object_name, path } => (Some(build_element_ref(&mut b, object_name, path)), false, 0, 0),
        InputTarget::Point { x, y } => (None, true, *x, *y),
    };
    let c = fb::SendInput::create(
        &mut b,
        &fb::SendInputArgs { target, events: Some(events), use_target_point, target_x, target_y },
    );
    finish(b, request_id, fb::Command::SendInput, c.as_union_value())
}

/// A Qt::Key code for a key name (Enter, Escape, Tab, Backspace, Delete, Space,
/// Left/Up/Right/Down, Home, End, PageUp, PageDown, F1-F12), a single
/// character, or a number given as is.
pub fn qt_key(name: &str) -> Option<u32> {
    let name = name.trim();
    if let Ok(code) = name.parse::<u32>() {
        if name.len() > 1 {
            return Some(code);
        }
    }
    let mut chars = name.chars();
    if let (Some(c), None) = (chars.next(), chars.next()) {
        // Qt::Key for a printable character is its upper-case Latin-1 code.
        return Some(c.to_ascii_uppercase() as u32);
    }
    let lower = name.to_ascii_lowercase();
    if let Some(n) = lower.strip_prefix('f').and_then(|n| n.parse::<u32>().ok()).filter(|n| (1..=12).contains(n)) {
        return Some(0x0100_0030 + n - 1);
    }
    Some(match lower.as_str() {
        "escape" | "esc" => 0x0100_0000,
        "tab" => 0x0100_0001,
        "backspace" => 0x0100_0003,
        "return" | "enter" => 0x0100_0004,
        "delete" | "del" => 0x0100_0007,
        "home" => 0x0100_0010,
        "end" => 0x0100_0011,
        "left" => 0x0100_0012,
        "up" => 0x0100_0013,
        "right" => 0x0100_0014,
        "down" => 0x0100_0015,
        "pageup" | "page_up" => 0x0100_0016,
        "pagedown" | "page_down" => 0x0100_0017,
        "space" => 0x20,
        _ => return None,
    })
}

/// The screen as a user sees it, downscaled to `max_width` when non-zero;
/// `region` (`[x, y, w, h]` in the frame's own pixels) crops it first.
pub fn get_frame(request_id: u64, max_width: u32, region: Option<[i32; 4]>) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let rect = region.map(|r| fb::Rect::new(r[0], r[1], r[2], r[3]));
    let c = fb::GetFrame::create(
        &mut b,
        &fb::GetFrameArgs { format: fb::FrameFormat::Png, max_width, region: rect.as_ref() },
    );
    finish(b, request_id, fb::Command::GetFrame, c.as_union_value())
}

pub fn scroll_to_line(request_id: u64, file_path: &str, line: u32) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let p = b.create_string(file_path);
    let c = fb::ScrollToLine::create(&mut b, &fb::ScrollToLineArgs { file_path: Some(p), line });
    finish(b, request_id, fb::Command::ScrollToLine, c.as_union_value())
}

/// The toolbar's back (`forward == false`) or forward navigation.
pub fn history(request_id: u64, forward: bool) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    if forward {
        let c = fb::HistoryForward::create(&mut b, &fb::HistoryForwardArgs {});
        finish(b, request_id, fb::Command::HistoryForward, c.as_union_value())
    } else {
        let c = fb::HistoryBack::create(&mut b, &fb::HistoryBackArgs {});
        finish(b, request_id, fb::Command::HistoryBack, c.as_union_value())
    }
}

pub fn load_project(request_id: u64, project_file_path: &str) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let p = b.create_string(project_file_path);
    let c = fb::LoadProject::create(
        &mut b,
        &fb::LoadProjectArgs {
            project_file_path: Some(p),
            refresh_mode: fb::RefreshMode::None,
        },
    );
    finish(b, request_id, fb::Command::LoadProject, c.as_union_value())
}

/// Build an `InvokeAction` command. `path` is the ref's role/name/index steps
/// (as carried by a snapshot node's `ref`); `object_name` is its anchor (usually
/// empty today). `text` sets editable text/value when non-empty.
/// Build an `ElementRef` (object_name anchor + role/name/index path). Shared by the
/// commands that address an element.
fn build_element_ref<'a>(
    b: &mut FlatBufferBuilder<'a>,
    object_name: &str,
    path: &[(String, String, u32)],
) -> WIPOffset<fb::ElementRef<'a>> {
    let steps: Vec<_> = path
        .iter()
        .map(|(role, name, index)| {
            let r = b.create_string(role);
            let n = b.create_string(name);
            fb::PathStep::create(b, &fb::PathStepArgs { role: Some(r), name: Some(n), index: *index })
        })
        .collect();
    let path_vec = b.create_vector(&steps);
    let on = b.create_string(object_name);
    fb::ElementRef::create(b, &fb::ElementRefArgs { object_name: Some(on), path: Some(path_vec) })
}

pub fn invoke_action(
    request_id: u64,
    object_name: &str,
    path: &[(String, String, u32)],
    action: &str,
    text: &str,
    expect_hash: u64,
) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let target = build_element_ref(&mut b, object_name, path);
    let act = b.create_string(action);
    let txt = b.create_string(text);
    let c = fb::InvokeAction::create(
        &mut b,
        &fb::InvokeActionArgs { target: Some(target), action: Some(act), text: Some(txt), expect_hash },
    );
    finish(b, request_id, fb::Command::InvokeAction, c.as_union_value())
}

/// Build a `CaptureElement` command targeting the element addressed by
/// `object_name` + `path`. Reply is a `FrameEnvelope` on st.agent.frames.
pub fn capture_element(
    request_id: u64,
    object_name: &str,
    path: &[(String, String, u32)],
    include_properties: bool,
    expect_hash: u64,
) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let target = build_element_ref(&mut b, object_name, path);
    let c = fb::CaptureElement::create(
        &mut b,
        &fb::CaptureElementArgs {
            target: Some(target),
            format: fb::FrameFormat::Png,
            include_properties,
            expect_hash,
        },
    );
    finish(b, request_id, fb::Command::CaptureElement, c.as_union_value())
}

/// Build a `QueryUi` command. Reply is a `UiSnapshot` of the matched nodes on
/// st.agent.snapshot (so it decodes via `parse_ui_snapshot`).
pub fn query_ui(request_id: u64, jsonpath: &str) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let jp = b.create_string(jsonpath);
    let c = fb::QueryUi::create(&mut b, &fb::QueryUiArgs { jsonpath: Some(jp) });
    finish(b, request_id, fb::Command::QueryUi, c.as_union_value())
}

/// Build a `SetLogFilter` command. `min_level`: "info" | "warning" | "error".
pub fn set_log_filter(request_id: u64, qt_rules: &str, event_pattern: &str, min_level: &str) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let qr = b.create_string(qt_rules);
    let ep = b.create_string(event_pattern);
    let level = match min_level {
        "info" => fb::LogLevel::Info,
        "error" => fb::LogLevel::Error,
        _ => fb::LogLevel::Warning,
    };
    let c = fb::SetLogFilter::create(
        &mut b,
        &fb::SetLogFilterArgs { qt_rules: Some(qr), event_pattern: Some(ep), min_level: level },
    );
    finish(b, request_id, fb::Command::SetLogFilter, c.as_union_value())
}

/// Decode a `FrameEnvelope`; returns `(request_id, frame_json)` with the payload
/// base64-encoded for JSON transport. Single-chunk only (chunk_count == 1).
pub fn parse_frame(bytes: &[u8]) -> Result<(u64, Value)> {
    use base64::Engine as _;
    let env = flatbuffers::root::<fb::FrameEnvelope>(bytes).map_err(|e| anyhow!("bad FrameEnvelope: {e}"))?;
    let payload = env.payload().map(|p| p.bytes()).unwrap_or_default();
    let json = json!({
        "width": env.width(),
        "height": env.height(),
        "format": "png",
        "bytes": payload.len(),
        "image_base64": base64::engine::general_purpose::STANDARD.encode(payload),
    });
    Ok((env.request_id(), json))
}

// --- Event decoding (st.agent.events) ---------------------------------------

/// A decoded event relevant to correlation / observation.
#[derive(Debug, Clone)]
pub enum Incoming {
    CommandResult { request_id: u64, ok: bool, message: String },
    SearchCompleted(Value),
    AppStateChanged(i8),
    TabsChanged(Value),
    Settled { request_id: u64 },
    AppInfo { request_id: u64, protocol_version: u32, app_version: String, build_id: String, instance_id: String },
    /// The answer to GetMachines / SendMachineEvent: the machines as JSON.
    MachineStates { request_id: u64, machines: Vec<Value> },
    /// A state machine moved (see [`machine_transition_json`]).
    MachineTransition(Value),
    Other(&'static str),
}

/// Decode one `EventEnvelope`; returns `(seq, Incoming)`.
pub fn parse_event(bytes: &[u8]) -> Result<(u64, Incoming)> {
    let env = flatbuffers::root::<fb::EventEnvelope>(bytes)
        .map_err(|e| anyhow!("bad EventEnvelope: {e}"))?;
    let seq = env.seq();
    let incoming = match env.event_type() {
        fb::Event::CommandResult => {
            let r = env.event_as_command_result().unwrap();
            Incoming::CommandResult {
                request_id: r.request_id(),
                ok: r.ok(),
                message: r.message().unwrap_or_default().to_string(),
            }
        }
        fb::Event::SearchCompleted => {
            let s = env.event_as_search_completed().unwrap();
            let matches: Vec<Value> = s
                .matches()
                .map(|v| v.iter().map(search_match_json).collect())
                .unwrap_or_default();
            Incoming::SearchCompleted(json!(matches))
        }
        fb::Event::AppStateChanged => {
            Incoming::AppStateChanged(env.event_as_app_state_changed().unwrap().state().0)
        }
        fb::Event::TabsChanged => {
            let t = env.event_as_tabs_changed().unwrap();
            let tabs: Vec<Value> =
                t.tabs().map(|v| v.iter().map(tab_info_json).collect()).unwrap_or_default();
            Incoming::TabsChanged(json!(tabs))
        }
        fb::Event::Settled => {
            Incoming::Settled { request_id: env.event_as_settled().unwrap().request_id() }
        }
        fb::Event::AppInfo => {
            let a = env.event_as_app_info().unwrap();
            Incoming::AppInfo {
                request_id: a.request_id(),
                protocol_version: a.protocol_version(),
                app_version: a.app_version().unwrap_or_default().to_string(),
                build_id: a.build_id().unwrap_or_default().to_string(),
                instance_id: a.instance_id().unwrap_or_default().to_string(),
            }
        }
        fb::Event::MachineStates => {
            let m = env.event_as_machine_states().unwrap();
            Incoming::MachineStates {
                request_id: m.request_id(),
                machines: m.machines().map(|v| v.iter().map(machine_state_json).collect()).unwrap_or_default(),
            }
        }
        fb::Event::MachineTransition => {
            Incoming::MachineTransition(machine_transition_json(env.event_as_machine_transition().unwrap()))
        }
        other => Incoming::Other(other.variant_name().unwrap_or("Unknown")),
    };
    Ok((seq, incoming))
}

/// Decode any `EventEnvelope` to a self-describing JSON object
/// `{ seq, timestamp_ms, type, ...fields }` for the observation stream
/// (`Bridge::poll_events`). Covers every `Event` union arm — unlike
/// [`parse_event`], which decodes only the arms used for reply correlation.
pub fn event_to_json(bytes: &[u8]) -> Result<Value> {
    let env = flatbuffers::root::<fb::EventEnvelope>(bytes)
        .map_err(|e| anyhow!("bad EventEnvelope: {e}"))?;
    let mut obj = json!({
        "seq": env.seq(),
        "timestamp_ms": env.timestamp_ms(),
        "type": env.event_type().variant_name().unwrap_or("Unknown"),
    });
    let m = obj.as_object_mut().expect("json object");
    // The request it follows from (protocol v7); left out for none.
    if env.caused_by() != 0 {
        m.insert("caused_by".into(), json!(env.caused_by()));
    }
    match env.event_type() {
        fb::Event::NodesActivated => {
            let e = env.event_as_nodes_activated().unwrap();
            let nodes: Vec<Value> = e.nodes().map(|v| v.iter().map(node_ref_json).collect()).unwrap_or_default();
            m.insert("nodes".into(), json!(nodes));
        }
        fb::Event::EdgeActivated => {
            if let Some(edge) = env.event_as_edge_activated().unwrap().edge() {
                m.insert("edge".into(), edge_ref_json(edge));
            }
        }
        fb::Event::FileActivated => {
            let e = env.event_as_file_activated().unwrap();
            m.insert("file_path".into(), json!(e.file_path().unwrap_or_default()));
        }
        fb::Event::SearchCompleted => {
            let e = env.event_as_search_completed().unwrap();
            let matches: Vec<Value> =
                e.matches().map(|v| v.iter().map(search_match_json).collect()).unwrap_or_default();
            m.insert("matches".into(), json!(matches));
        }
        fb::Event::FocusChanged => {
            m.insert("view".into(), json!(env.event_as_focus_changed().unwrap().view().0));
        }
        fb::Event::IndexingProgress => {
            let e = env.event_as_indexing_progress().unwrap();
            m.insert("files_done".into(), json!(e.files_done()));
            m.insert("files_total".into(), json!(e.files_total()));
        }
        fb::Event::IndexingFinished => {
            m.insert("succeeded".into(), json!(env.event_as_indexing_finished().unwrap().succeeded()));
        }
        fb::Event::ErrorCountChanged => {
            let e = env.event_as_error_count_changed().unwrap();
            m.insert("total".into(), json!(e.total()));
            m.insert("fatal".into(), json!(e.fatal()));
        }
        fb::Event::StatusChanged => {
            m.insert("text".into(), json!(env.event_as_status_changed().unwrap().text().unwrap_or_default()));
        }
        fb::Event::AppStateChanged => {
            m.insert("state".into(), json!(app_state_str(env.event_as_app_state_changed().unwrap().state().0)));
        }
        fb::Event::TabsChanged => {
            let e = env.event_as_tabs_changed().unwrap();
            let tabs: Vec<Value> = e.tabs().map(|v| v.iter().map(tab_info_json).collect()).unwrap_or_default();
            m.insert("tabs".into(), json!(tabs));
        }
        fb::Event::CommandResult => {
            let e = env.event_as_command_result().unwrap();
            m.insert("request_id".into(), json!(e.request_id()));
            m.insert("ok".into(), json!(e.ok()));
            m.insert("message".into(), json!(e.message().unwrap_or_default()));
        }
        fb::Event::Settled => {
            m.insert("request_id".into(), json!(env.event_as_settled().unwrap().request_id()));
        }
        fb::Event::LogEvent => {
            let e = env.event_as_log_event().unwrap();
            let level = match e.level().0 {
                0 => "info",
                2 => "error",
                _ => "warning",
            };
            m.insert("level".into(), json!(level));
            m.insert("log_source".into(), json!(if e.source() == fb::LogSource::Qt { "qt" } else { "app" }));
            m.insert("category".into(), json!(e.category().unwrap_or_default()));
            m.insert("message".into(), json!(e.message().unwrap_or_default()));
            m.insert("log_file".into(), json!(e.file().unwrap_or_default()));
            m.insert("function".into(), json!(e.function().unwrap_or_default()));
            m.insert("log_line".into(), json!(e.line()));
        }
        fb::Event::AppInfo => {
            let e = env.event_as_app_info().unwrap();
            m.insert("request_id".into(), json!(e.request_id()));
            m.insert("protocol_version".into(), json!(e.protocol_version()));
            m.insert("app_version".into(), json!(e.app_version().unwrap_or_default()));
            m.insert("build_id".into(), json!(e.build_id().unwrap_or_default()));
            m.insert("instance_id".into(), json!(e.instance_id().unwrap_or_default()));
            let caps: Vec<&str> = e.capabilities().map(|v| v.iter().collect()).unwrap_or_default();
            m.insert("capabilities".into(), json!(caps));
        }
        fb::Event::MachineTransition => {
            if let Value::Object(fields) = machine_transition_json(env.event_as_machine_transition().unwrap()) {
                m.extend(fields);
            }
        }
        fb::Event::MachineStates => {
            let e = env.event_as_machine_states().unwrap();
            m.insert("request_id".into(), json!(e.request_id()));
            let machines: Vec<Value> =
                e.machines().map(|v| v.iter().map(machine_state_json).collect()).unwrap_or_default();
            m.insert("machines".into(), json!(machines));
        }
        fb::Event::DialogChanged => {
            m.insert("dialog".into(), json!(env.event_as_dialog_changed().unwrap().dialog().unwrap_or_default()));
        }
        // IndexingStarted (empty) and any future arms: type tag only.
        _ => {}
    }
    Ok(obj)
}

// TEMPORARY diagnostic: dump a buffer that failed to verify.
fn dump_bad_buffer(tag: &str, bytes: &[u8]) {
    if std::env::var("SOURCETRAIL_MCP_DUMP_BAD").is_err() { return; }
    let path = format!("/tmp/badbuf-{tag}-{}.bin", bytes.len());
    let _ = std::fs::write(&path, bytes);
    eprintln!("dumped {} bytes to {} (ptr%8 unknown)", bytes.len(), path);
}

// --- UiState decoding (st.agent.state) --------------------------------------

/// Decode a `UiStateEnvelope`; returns `(request_id, ui_state_json)`.
pub fn parse_ui_state(bytes: &[u8]) -> Result<(u64, Value)> {
    let env = flatbuffers::root::<fb::UiStateEnvelope>(bytes)
        .map_err(|e| { dump_bad_buffer("uistate", bytes); anyhow!("bad UiStateEnvelope: {e}") })?;
    let request_id = env.request_id();
    let s = env.state().ok_or_else(|| anyhow!("UiStateEnvelope has no state"))?;

    let active_nodes: Vec<Value> = s
        .active_nodes()
        .map(|v| v.iter().map(node_ref_json).collect())
        .unwrap_or_default();

    let graph = s.graph().map(|g| {
        let nodes: Vec<Value> = g.nodes().map(|v| v.iter().map(node_ref_json).collect()).unwrap_or_default();
        let edges: Vec<Value> = g.edges().map(|v| v.iter().map(edge_ref_json).collect()).unwrap_or_default();
        json!({ "nodes": nodes, "edges": edges })
    });

    let code_view = s.code_view().map(|c| {
        json!({ "file_path": c.file_path().unwrap_or_default(), "scroll_line": c.scroll_line() })
    });

    let search_matches: Vec<Value> = s
        .search_matches()
        .map(|v| v.iter().map(search_match_json).collect())
        .unwrap_or_default();

    let tabs: Vec<Value> = s.tabs().map(|v| v.iter().map(tab_info_json).collect()).unwrap_or_default();

    let json = json!({
        "request_id": request_id,
        "project_file_path": s.project_file_path().unwrap_or_default(),
        "app_state": app_state_str(s.app_state().0),
        // What waits for an answer in front of the app: "" when nothing does, null
        // from an app before protocol v3, which cannot tell.
        "dialog": s.dialog(),
        "indexing_active": s.indexing_active(),
        "error_total": s.error_total(),
        "error_fatal": s.error_fatal(),
        "active_nodes": active_nodes,
        "graph": graph,
        "code_view": code_view,
        "search_matches": search_matches,
        "tabs": tabs,
        // The state machines (protocol v6); null from an older app.
        "machines": s.machines().map(|v| v.iter().map(machine_state_json).collect::<Vec<_>>()),
        // The graph as drawn (protocol v7); null from an older app.
        "view_graph": s.view_graph().map(|v| v.iter().map(graph_view_node_json).collect::<Vec<_>>()),
    });
    Ok((request_id, json))
}

// --- UiSnapshot decoding (st.agent.snapshot) --------------------------------

/// Decode a `UiSnapshot`; returns `(request_id, tree_json)`.
pub fn parse_ui_snapshot(bytes: &[u8]) -> Result<(u64, Value)> {
    let snap =
        flatbuffers::root::<fb::UiSnapshot>(bytes).map_err(|e| anyhow!("bad UiSnapshot: {e}"))?;
    let roots: Vec<Value> = snap
        .roots()
        .map(|v| v.iter().map(ui_node_json).collect())
        .unwrap_or_default();
    let format = if snap.format() == fb::SnapshotFormat::ObjectTree {
        "objectTree"
    } else {
        "accessibility"
    };
    let json = json!({
        "request_id": snap.request_id(),
        "format": format,
        "tree_hash": snap.tree_hash(),
        "roots": roots,
    });
    Ok((snap.request_id(), json))
}

fn ui_node_json(n: fb::UiNode) -> Value {
    let children: Vec<Value> = n
        .children()
        .map(|v| v.iter().map(ui_node_json).collect())
        .unwrap_or_default();
    let actions: Vec<&str> = n.actions().map(|v| v.iter().collect()).unwrap_or_default();
    let rect = n
        .rect()
        .map(|r| json!({ "x": r.x(), "y": r.y(), "w": r.w(), "h": r.h() }));
    json!({
        "role": n.role().unwrap_or_default(),
        "name": n.name().unwrap_or_default(),
        "value": n.value().unwrap_or_default(),
        "state": n.state(),
        "actions": actions,
        "rect": rect,
        "ref": n.ref_().map(element_ref_json),
        "hash": n.hash(),
        "children": children,
    })
}

fn element_ref_json(e: fb::ElementRef) -> Value {
    let path: Vec<Value> = e
        .path()
        .map(|v| {
            v.iter()
                .map(|s| json!({ "role": s.role().unwrap_or_default(), "name": s.name().unwrap_or_default(), "index": s.index() }))
                .collect()
        })
        .unwrap_or_default();
    json!({ "object_name": e.object_name().unwrap_or_default(), "path": path })
}

// --- small projections ------------------------------------------------------

fn node_ref_json(n: fb::NodeRef) -> Value {
    json!({
        "node_id": n.node_id(),
        "serialized_name": n.serialized_name().unwrap_or_default(),
        "display_name": n.display_name().unwrap_or_default(),
        "node_kind": n.node_kind(),
    })
}

fn edge_ref_json(e: fb::EdgeRef) -> Value {
    json!({
        "edge_id": e.edge_id(),
        "edge_type": e.edge_type(),
        "source": e.source_serialized_name().unwrap_or_default(),
        "target": e.target_serialized_name().unwrap_or_default(),
    })
}

fn search_match_json(m: fb::SearchMatch) -> Value {
    json!({
        "text": m.text().unwrap_or_default(),
        "node_kind": m.node_kind(),
        "node_ids": m.node_ids().map(|v| v.iter().collect::<Vec<u64>>()).unwrap_or_default(),
        "score": m.score(),
    })
}

fn tab_info_json(t: fb::TabInfo) -> Value {
    json!({
        "tab_id": t.tab_id(),
        "title": t.title().unwrap_or_default(),
        "active": t.active(),
    })
}

/// A machine's context is a JSON object in a string; hand it on as JSON (or as
/// the string, should it ever not parse).
fn context_json(text: Option<&str>) -> Value {
    let text = text.unwrap_or_default();
    if text.is_empty() {
        return json!({});
    }
    serde_json::from_str(text).unwrap_or_else(|_| json!(text))
}

pub fn machine_state_json(m: fb::MachineState) -> Value {
    let events: Vec<&str> = m.events().map(|v| v.iter().collect()).unwrap_or_default();
    json!({
        "machine": m.machine().unwrap_or_default(),
        "state": m.state().unwrap_or_default(),
        "events": events,
        "context": context_json(m.context()),
        "revision": m.revision(),
    })
}

pub fn machine_transition_json(t: fb::MachineTransition) -> Value {
    json!({
        "machine": t.machine().unwrap_or_default(),
        "from": t.from().unwrap_or_default(),
        "to": t.to().unwrap_or_default(),
        "event": t.event().unwrap_or_default(),
        "source": t.source().unwrap_or_default(),
        "context": context_json(t.context()),
        "revision": t.revision(),
    })
}

fn app_state_str(v: i8) -> &'static str {
    match v {
        1 => "Loading",
        2 => "Indexing",
        3 => "Ready",
        4 => "Busy",
        _ => "NoProject",
    }
}

// --- contract round-trip tests (no thoth-ipc / no app) ----------------------
// Build each envelope exactly as the C++ side would, decode it back through the
// bridge's builders/parsers, and assert. Validates the FlatBuffers contract end to
// end without the transport; becomes the regression harness once the shm-naming
// fix lands.
#[cfg(test)]
mod tests {
    use super::*;

    fn cmd(bytes: &[u8]) -> fb::CommandEnvelope<'_> {
        flatbuffers::root::<fb::CommandEnvelope>(bytes).expect("valid CommandEnvelope")
    }

    #[test]
    fn search_command_roundtrips() {
        let bytes = search(42, "parseArgs");
        let env = cmd(&bytes);
        assert_eq!(env.request_id(), 42);
        assert_eq!(env.command_type(), fb::Command::Search);
        assert_eq!(env.command_as_search().unwrap().query().unwrap(), "parseArgs");
    }

    #[test]
    fn activate_node_command_roundtrips() {
        let bytes = activate_node(7, Some("m::foo"), 99);
        let env = cmd(&bytes);
        assert_eq!(env.command_type(), fb::Command::ActivateNode);
        let c = env.command_as_activate_node().unwrap();
        assert_eq!(c.serialized_name().unwrap(), "m::foo");
        assert_eq!(c.node_id(), 99);
    }

    #[test]
    fn activate_tab_command_roundtrips() {
        let bytes = activate_tab(3, 10);
        let env = cmd(&bytes);
        assert_eq!(env.request_id(), 3);
        assert_eq!(env.command_type(), fb::Command::ActivateTab);
        assert_eq!(env.command_as_activate_tab().unwrap().tab_id(), 10);
    }

    #[test]
    fn scroll_to_line_command_roundtrips() {
        let bytes = scroll_to_line(4, "/src/main.cpp", 42);
        let env = cmd(&bytes);
        assert_eq!(env.command_type(), fb::Command::ScrollToLine);
        let c = env.command_as_scroll_to_line().unwrap();
        assert_eq!(c.file_path(), Some("/src/main.cpp"));
        assert_eq!(c.line(), 42);
    }

    #[test]
    fn get_frame_command_roundtrips() {
        let whole_bytes = get_frame(8, 1024, None);
        let whole = cmd(&whole_bytes).command_as_get_frame().unwrap();
        assert_eq!(whole.max_width(), 1024);
        assert!(whole.region().is_none());
        let bytes = get_frame(9, 0, Some([10, 20, 300, 200]));
        let env = cmd(&bytes);
        assert_eq!(env.command_type(), fb::Command::GetFrame);
        let r = env.command_as_get_frame().unwrap().region().copied().unwrap();
        assert_eq!((r.x(), r.y(), r.w(), r.h()), (10, 20, 300, 200));
    }

    #[test]
    fn send_input_command_roundtrips() {
        let click = [
            InputEvent { kind: InputKind::Press, centered: true, button: 1, ..Default::default() },
            InputEvent { kind: InputKind::Release, x: 3, y: 4, button: 1, modifiers: 0b10, ..Default::default() },
            InputEvent { kind: InputKind::KeyPress, key: 0x0100_0004, text: "\r".into(), ..Default::default() },
        ];
        let at_point = send_input(1, &InputTarget::Point { x: 500, y: 300 }, &click);
        let c = cmd(&at_point).command_as_send_input().unwrap();
        assert!(c.use_target_point());
        assert_eq!((c.target_x(), c.target_y()), (500, 300));
        let events = c.events().unwrap();
        assert_eq!(events.len(), 3);
        assert_eq!(events.get(0).kind(), fb::InputEventKind::MousePress);
        assert!(events.get(0).centered());
        assert_eq!(events.get(1).button(), fb::MouseButton::Left);
        assert_eq!((events.get(1).x(), events.get(1).y(), events.get(1).modifiers()), (3, 4, 2));
        assert_eq!(events.get(2).key(), 0x0100_0004);

        let path = vec![("button".to_string(), "Back".to_string(), 0)];
        let by_ref = send_input(2, &InputTarget::Ref { object_name: "undo_button", path: &path }, &click[..1]);
        let c = cmd(&by_ref).command_as_send_input().unwrap();
        assert!(!c.use_target_point());
        assert_eq!(c.target().unwrap().object_name(), Some("undo_button"));
    }

    #[test]
    fn keys_and_kinds_parse() {
        assert_eq!(qt_key("a"), Some(0x41));
        assert_eq!(qt_key("Enter"), Some(0x0100_0004));
        assert_eq!(qt_key("F5"), Some(0x0100_0034));
        assert_eq!(qt_key("16777216"), Some(0x0100_0000));
        assert_eq!(qt_key("bogus"), None);
        assert_eq!(InputKind::parse("DoubleClick"), Some(InputKind::DoubleClick));
        assert_eq!(InputKind::parse("tap"), None);
    }

    #[test]
    fn history_commands_roundtrip() {
        assert_eq!(cmd(&history(6, false)).command_type(), fb::Command::HistoryBack);
        assert_eq!(cmd(&history(7, true)).command_type(), fb::Command::HistoryForward);
    }

    #[test]
    fn get_ui_state_command_roundtrips() {
        let bytes = get_ui_state(5);
        let env = cmd(&bytes);
        assert_eq!(env.request_id(), 5);
        assert_eq!(env.command_type(), fb::Command::GetUiState);
    }

    #[test]
    fn load_project_command_roundtrips() {
        let bytes = load_project(1, "/p/x.srctrlprj");
        let env = cmd(&bytes);
        let c = env.command_as_load_project().unwrap();
        assert_eq!(c.project_file_path().unwrap(), "/p/x.srctrlprj");
    }

    #[test]
    fn events_say_what_they_follow_from() {
        let mut b = FlatBufferBuilder::new();
        let dialog = b.create_string("'Reindex?' (modal)");
        let e = fb::DialogChanged::create(&mut b, &fb::DialogChangedArgs { dialog: Some(dialog) });
        let env = fb::EventEnvelope::create(
            &mut b,
            &fb::EventEnvelopeArgs {
                seq: 3,
                timestamp_ms: 10,
                event_type: fb::Event::DialogChanged,
                event: Some(e.as_union_value()),
                caused_by: 42,
            },
        );
        b.finish(env, None);
        let v = event_to_json(b.finished_data()).unwrap();
        assert_eq!(v["type"], "DialogChanged");
        assert_eq!(v["dialog"], "'Reindex?' (modal)");
        assert_eq!(v["caused_by"], 42);

        // No cause: left out, not 0.
        let mut b = FlatBufferBuilder::new();
        let s = fb::Settled::create(&mut b, &fb::SettledArgs { request_id: 5 });
        let env = fb::EventEnvelope::create(
            &mut b,
            &fb::EventEnvelopeArgs {
                seq: 4,
                timestamp_ms: 11,
                event_type: fb::Event::Settled,
                event: Some(s.as_union_value()),
                caused_by: 0,
            },
        );
        b.finish(env, None);
        assert!(event_to_json(b.finished_data()).unwrap().get("caused_by").is_none());
    }

    #[test]
    fn ui_state_reports_its_dialog() {
        fn with_dialog(dialog: &str) -> Value {
            let mut b = FlatBufferBuilder::new();
            let text = b.create_string(dialog);
            let ui = fb::UiState::create(&mut b, &fb::UiStateArgs { dialog: Some(text), ..Default::default() });
            let env = fb::UiStateEnvelope::create(&mut b, &fb::UiStateEnvelopeArgs { request_id: 1, state: Some(ui) });
            b.finish(env, None);
            parse_ui_state(b.finished_data()).unwrap().1
        }
        assert_eq!(with_dialog("Start Indexing")["dialog"], "Start Indexing");
        // Nothing in front: an empty string, which a v3 app always writes.
        assert_eq!(with_dialog("")["dialog"], "");
    }

    #[test]
    fn ui_state_parses_to_json() {
        let mut b = FlatBufferBuilder::new();
        let sn = b.create_string("m::Foo");
        let dn = b.create_string("Foo");
        let node = fb::NodeRef::create(
            &mut b,
            &fb::NodeRefArgs { node_id: 100, serialized_name: Some(sn), node_kind: 1, display_name: Some(dn) },
        );
        let active = b.create_vector(&[node]);
        let title = b.create_string("Foo");
        let tab = fb::TabInfo::create(&mut b, &fb::TabInfoArgs { tab_id: 10, title: Some(title), active: true });
        let tabs = b.create_vector(&[tab]);
        let proj = b.create_string("/p/proj.srctrlprj");
        let ui = fb::UiState::create(
            &mut b,
            &fb::UiStateArgs {
                project_file_path: Some(proj),
                active_nodes: Some(active),
                tabs: Some(tabs),
                app_state: fb::AppState::Ready,
                error_total: 2,
                error_fatal: 1,
                ..Default::default()
            },
        );
        let env = fb::UiStateEnvelope::create(&mut b, &fb::UiStateEnvelopeArgs { request_id: 9, state: Some(ui) });
        b.finish(env, None);

        let (rid, json) = parse_ui_state(b.finished_data()).unwrap();
        assert_eq!(rid, 9);
        assert_eq!(json["app_state"], "Ready");
        // No dialog field: an app before v3, which cannot tell, reads as null.
        assert!(json["dialog"].is_null());
        assert_eq!(json["error_total"], 2);
        assert_eq!(json["active_nodes"][0]["serialized_name"], "m::Foo");
        assert_eq!(json["tabs"][0]["tab_id"], 10);
        assert_eq!(json["tabs"][0]["active"], true);
    }

    // A SearchMatch with no node ids must be written with node_ids *absent*, not
    // as a zero-length vector — see makeSearchMatch in AgentControlController.cpp.
    //
    // node_ids is [uint64], so its elements must start 8-aligned. An empty vector
    // has no elements, so the builder does not force that boundary and the buffer
    // is legal; but flatbuffers' verifier checks is_aligned::<T>(start) before it
    // looks at the length (verify_vector_range), and rejects the whole message
    // over the alignment of elements that are not there. Whether it trips depends
    // on what precedes the vector, which is why it looked intermittent.
    //
    // Note the hazard cannot be built from this side: Rust's builder aligns even
    // a zero-length vector, so the empty-vector variants below all pass here
    // (the counter prints 0). Only the C++ writer produces the refused shape,
    // which is why the fix lives there and this test guards the invariant rather
    // than reproducing the fault. The reproduction is a live one: search for a
    // command name ("boundary"), then GetUiState.
    #[test]
    fn absent_node_ids_parses_at_every_alignment() {
        fn build(pad: usize, empty_vector: bool) -> Vec<u8> {
            let mut b = FlatBufferBuilder::new();
            let text = b.create_string(&"x".repeat(pad));
            let ids = if empty_vector { Some(b.create_vector::<u64>(&[])) } else { None };
            let m = fb::SearchMatch::create(
                &mut b,
                &fb::SearchMatchArgs { text: Some(text), node_kind: 0, node_ids: ids, score: 1 },
            );
            let matches = b.create_vector(&[m]);
            let ui = fb::UiState::create(
                &mut b,
                &fb::UiStateArgs { search_matches: Some(matches), ..Default::default() },
            );
            let env = fb::UiStateEnvelope::create(
                &mut b, &fb::UiStateEnvelopeArgs { request_id: 1, state: Some(ui) });
            b.finish(env, None);
            b.finished_data().to_vec()
        }

        let mut refused_when_empty = 0;
        for pad in 0..24 {
            let absent = build(pad, false);
            let (_, json) = parse_ui_state(&absent)
                .unwrap_or_else(|e| panic!("absent node_ids refused at pad {pad}: {e}"));
            assert_eq!(json["search_matches"][0]["node_ids"].as_array().unwrap().len(), 0);

            if parse_ui_state(&build(pad, true)).is_err() {
                refused_when_empty += 1;
            }
        }
        println!("empty-vector form refused at {refused_when_empty} of 24 alignments");
    }

    // search_matches is the only field in the envelope carrying a [uint64], and
    // it went untested above -- which is how a misaligned node_ids vector
    // reached the wire unnoticed. Kept as its own case so the u64 vector is
    // always exercised.
    #[test]
    fn ui_state_search_matches_with_node_ids_parse() {
        let mut b = FlatBufferBuilder::new();
        let text = b.create_string("boundary");
        let ids = b.create_vector(&[7u64, 8u64, 9u64]);
        let m = fb::SearchMatch::create(
            &mut b,
            &fb::SearchMatchArgs { text: Some(text), node_kind: 0, node_ids: Some(ids), score: 5 },
        );
        let matches = b.create_vector(&[m]);
        let ui = fb::UiState::create(
            &mut b,
            &fb::UiStateArgs { search_matches: Some(matches), ..Default::default() },
        );
        let env = fb::UiStateEnvelope::create(
            &mut b, &fb::UiStateEnvelopeArgs { request_id: 3, state: Some(ui) });
        b.finish(env, None);

        let (rid, json) = parse_ui_state(b.finished_data()).unwrap();
        assert_eq!(rid, 3);
        assert_eq!(json["search_matches"][0]["text"], "boundary");
        assert_eq!(json["search_matches"][0]["node_ids"][0], 7);
        assert_eq!(json["search_matches"][0]["node_ids"][2], 9);
    }

    #[test]
    fn command_result_event_parses() {
        let mut b = FlatBufferBuilder::new();
        let msg = b.create_string("rejected: no project loaded");
        let cr = fb::CommandResult::create(
            &mut b,
            &fb::CommandResultArgs { request_id: 4, ok: false, message: Some(msg) },
        );
        let env = fb::EventEnvelope::create(
            &mut b,
            &fb::EventEnvelopeArgs {
                seq: 1,
                timestamp_ms: 0,
                event_type: fb::Event::CommandResult,
                event: Some(cr.as_union_value()),
                caused_by: 0,
            },
        );
        b.finish(env, None);

        let (seq, inc) = parse_event(b.finished_data()).unwrap();
        assert_eq!(seq, 1);
        match inc {
            Incoming::CommandResult { request_id, ok, message } => {
                assert_eq!(request_id, 4);
                assert!(!ok);
                assert!(message.starts_with("rejected"));
            }
            other => panic!("expected CommandResult, got {other:?}"),
        }
    }

    #[test]
    fn tabs_changed_event_parses() {
        let mut b = FlatBufferBuilder::new();
        let title = b.create_string("Bar");
        let tab = fb::TabInfo::create(&mut b, &fb::TabInfoArgs { tab_id: 11, title: Some(title), active: true });
        let tabs = b.create_vector(&[tab]);
        let tc = fb::TabsChanged::create(&mut b, &fb::TabsChangedArgs { tabs: Some(tabs) });
        let env = fb::EventEnvelope::create(
            &mut b,
            &fb::EventEnvelopeArgs {
                seq: 2,
                timestamp_ms: 0,
                event_type: fb::Event::TabsChanged,
                event: Some(tc.as_union_value()),
                caused_by: 0,
            },
        );
        b.finish(env, None);

        match parse_event(b.finished_data()).unwrap().1 {
            Incoming::TabsChanged(v) => {
                assert_eq!(v[0]["tab_id"], 11);
                assert_eq!(v[0]["active"], true);
            }
            other => panic!("expected TabsChanged, got {other:?}"),
        }
    }

    #[test]
    fn event_to_json_decodes_all_arms() {
        // IndexingProgress — a push-only event parse_event doesn't model.
        let mut b = FlatBufferBuilder::new();
        let ip = fb::IndexingProgress::create(
            &mut b,
            &fb::IndexingProgressArgs { files_done: 7, files_total: 20 },
        );
        let env = fb::EventEnvelope::create(
            &mut b,
            &fb::EventEnvelopeArgs {
                seq: 42,
                timestamp_ms: 1234,
                event_type: fb::Event::IndexingProgress,
                event: Some(ip.as_union_value()),
                caused_by: 0,
            },
        );
        b.finish(env, None);
        let v = event_to_json(b.finished_data()).unwrap();
        assert_eq!(v["seq"], 42);
        assert_eq!(v["timestamp_ms"], 1234);
        assert_eq!(v["type"], "IndexingProgress");
        assert_eq!(v["files_done"], 7);
        assert_eq!(v["files_total"], 20);

        // AppStateChanged — decodes the enum to its string form.
        let mut b = FlatBufferBuilder::new();
        let asc = fb::AppStateChanged::create(
            &mut b,
            &fb::AppStateChangedArgs { state: fb::AppState::Ready },
        );
        let env = fb::EventEnvelope::create(
            &mut b,
            &fb::EventEnvelopeArgs {
                seq: 43,
                timestamp_ms: 0,
                event_type: fb::Event::AppStateChanged,
                event: Some(asc.as_union_value()),
                caused_by: 0,
            },
        );
        b.finish(env, None);
        let v = event_to_json(b.finished_data()).unwrap();
        assert_eq!(v["type"], "AppStateChanged");
        assert_eq!(v["state"], "Ready");
    }

    #[test]
    fn settled_event_parses() {
        let mut b = FlatBufferBuilder::new();
        let s = fb::Settled::create(&mut b, &fb::SettledArgs { request_id: 9 });
        let env = fb::EventEnvelope::create(
            &mut b,
            &fb::EventEnvelopeArgs {
                seq: 5,
                timestamp_ms: 0,
                event_type: fb::Event::Settled,
                event: Some(s.as_union_value()),
                caused_by: 0,
            },
        );
        b.finish(env, None);
        // correlation decode (await_settled path)
        match parse_event(b.finished_data()).unwrap().1 {
            Incoming::Settled { request_id } => assert_eq!(request_id, 9),
            other => panic!("expected Settled, got {other:?}"),
        }
        // general decode (poll_events path)
        let v = event_to_json(b.finished_data()).unwrap();
        assert_eq!(v["type"], "Settled");
        assert_eq!(v["request_id"], 9);
    }

    #[test]
    fn get_info_command_and_app_info_event_roundtrip() {
        // The command carries no fields beyond its id.
        let bytes = get_info(3);
        let env = flatbuffers::root::<fb::CommandEnvelope>(&bytes).unwrap();
        assert_eq!(env.request_id(), 3);
        assert_eq!(env.command_type(), fb::Command::GetInfo);

        // The handshake reply: build an AppInfo event and decode it both ways.
        let mut b = FlatBufferBuilder::new();
        let app_version = b.create_string("2025.1.42");
        let build_id = b.create_string("deadbeef");
        let instance_id = b.create_string("main-abc1234");
        let a = fb::AppInfo::create(
            &mut b,
            &fb::AppInfoArgs {
                request_id: 3,
                protocol_version: AGENT_PROTOCOL_VERSION,
                app_version: Some(app_version),
                build_id: Some(build_id),
                instance_id: Some(instance_id),
                capabilities: None,
            },
        );
        let env = fb::EventEnvelope::create(
            &mut b,
            &fb::EventEnvelopeArgs {
                seq: 11,
                timestamp_ms: 0,
                event_type: fb::Event::AppInfo,
                event: Some(a.as_union_value()),
                caused_by: 0,
            },
        );
        b.finish(env, None);
        // correlation decode (handshake path)
        match parse_event(b.finished_data()).unwrap().1 {
            Incoming::AppInfo { request_id, protocol_version, app_version, instance_id, .. } => {
                assert_eq!(request_id, 3);
                assert_eq!(protocol_version, AGENT_PROTOCOL_VERSION);
                assert_eq!(app_version, "2025.1.42");
                assert_eq!(instance_id, "main-abc1234");
            }
            other => panic!("expected AppInfo, got {other:?}"),
        }
        // general decode (poll_events path)
        let v = event_to_json(b.finished_data()).unwrap();
        assert_eq!(v["type"], "AppInfo");
        assert_eq!(v["request_id"], 3);
        assert_eq!(v["protocol_version"], AGENT_PROTOCOL_VERSION);
        assert_eq!(v["build_id"], "deadbeef");
    }

    #[test]
    fn invoke_action_command_roundtrips() {
        let path = vec![
            ("menu".to_string(), String::new(), 0u32),
            ("menuitem".to_string(), "New Project...".to_string(), 0u32),
        ];
        let bytes = invoke_action(7, "", &path, "Press", "", 0);
        let env = flatbuffers::root::<fb::CommandEnvelope>(&bytes).unwrap();
        assert_eq!(env.request_id(), 7);
        assert_eq!(env.command_type(), fb::Command::InvokeAction);
        let c = env.command_as_invoke_action().unwrap();
        assert_eq!(c.action().unwrap(), "Press");
        let steps = c.target().unwrap().path().unwrap();
        assert_eq!(steps.len(), 2);
        assert_eq!(steps.get(1).role().unwrap(), "menuitem");
        assert_eq!(steps.get(1).name().unwrap(), "New Project...");
    }

    #[test]
    fn capture_element_command_and_frame_roundtrip() {
        // command builds + decodes
        let path = vec![("button".to_string(), "OK".to_string(), 0u32)];
        let bytes = capture_element(3, "", &path, false, 0);
        let env = flatbuffers::root::<fb::CommandEnvelope>(&bytes).unwrap();
        assert_eq!(env.command_type(), fb::Command::CaptureElement);
        assert_eq!(env.command_as_capture_element().unwrap().target().unwrap().path().unwrap().len(), 1);

        // FrameEnvelope -> parse_frame (payload base64-encoded)
        let mut b = FlatBufferBuilder::new();
        let payload = b.create_vector(&[1u8, 2, 3, 4]);
        let fe = fb::FrameEnvelope::create(
            &mut b,
            &fb::FrameEnvelopeArgs {
                request_id: 3,
                width: 10,
                height: 20,
                chunk_count: 1,
                total_bytes: 4,
                payload: Some(payload),
                ..Default::default()
            },
        );
        b.finish(fe, None);
        let (rid, v) = parse_frame(b.finished_data()).unwrap();
        assert_eq!(rid, 3);
        assert_eq!(v["width"], 10);
        assert_eq!(v["bytes"], 4);
        assert_eq!(v["image_base64"], "AQIDBA==");
    }

    #[test]
    fn set_log_filter_and_log_event_roundtrip() {
        // command
        let bytes = set_log_filter(8, "qt.qpa.*=false", "network", "error");
        let env = flatbuffers::root::<fb::CommandEnvelope>(&bytes).unwrap();
        assert_eq!(env.command_type(), fb::Command::SetLogFilter);
        let c = env.command_as_set_log_filter().unwrap();
        assert_eq!(c.qt_rules().unwrap(), "qt.qpa.*=false");
        assert_eq!(c.event_pattern().unwrap(), "network");
        assert_eq!(c.min_level(), fb::LogLevel::Error);

        // LogEvent -> event_to_json
        let mut b = FlatBufferBuilder::new();
        let msg = b.create_string("connection failed");
        let cat = b.create_string("myapp.net");
        let le = fb::LogEvent::create(
            &mut b,
            &fb::LogEventArgs {
                level: fb::LogLevel::Error,
                source: fb::LogSource::Qt,
                category: Some(cat),
                message: Some(msg),
                line: 42,
                ..Default::default()
            },
        );
        let ev = fb::EventEnvelope::create(
            &mut b,
            &fb::EventEnvelopeArgs {
                seq: 3,
                timestamp_ms: 0,
                event_type: fb::Event::LogEvent,
                event: Some(le.as_union_value()),
                caused_by: 0,
            },
        );
        b.finish(ev, None);
        let v = event_to_json(b.finished_data()).unwrap();
        assert_eq!(v["type"], "LogEvent");
        assert_eq!(v["level"], "error");
        assert_eq!(v["log_source"], "qt");
        assert_eq!(v["category"], "myapp.net");
        assert_eq!(v["message"], "connection failed");
        assert_eq!(v["log_line"], 42);
    }

    #[test]
    fn query_ui_command_roundtrips() {
        let bytes = query_ui(5, "$.roots..[?(@.role=='button')]");
        let env = flatbuffers::root::<fb::CommandEnvelope>(&bytes).unwrap();
        assert_eq!(env.request_id(), 5);
        assert_eq!(env.command_type(), fb::Command::QueryUi);
        assert_eq!(env.command_as_query_ui().unwrap().jsonpath().unwrap(), "$.roots..[?(@.role=='button')]");
    }

    #[test]
    fn ui_snapshot_roundtrips() {
        let mut b = FlatBufferBuilder::new();

        // child: button "OK" with a Press action; ref path [window/Main, button/OK]
        let (wr, wn) = (b.create_string("window"), b.create_string("Main"));
        let s_win = fb::PathStep::create(&mut b, &fb::PathStepArgs { role: Some(wr), name: Some(wn), index: 0 });
        let (br, bn) = (b.create_string("button"), b.create_string("OK"));
        let s_btn = fb::PathStep::create(&mut b, &fb::PathStepArgs { role: Some(br), name: Some(bn), index: 0 });
        let child_path = b.create_vector(&[s_win, s_btn]);
        let child_ref = fb::ElementRef::create(&mut b, &fb::ElementRefArgs { object_name: None, path: Some(child_path) });
        let press = b.create_string("Press");
        let child_actions = b.create_vector(&[press]);
        let (crole, cname) = (b.create_string("button"), b.create_string("OK"));
        let child_rect = fb::Rect::new(0, 0, 40, 20);
        let child = fb::UiNode::create(
            &mut b,
            &fb::UiNodeArgs {
                ref_: Some(child_ref),
                role: Some(crole),
                name: Some(cname),
                actions: Some(child_actions),
                rect: Some(&child_rect),
                ..Default::default()
            },
        );
        let children = b.create_vector(&[child]);

        // root: window "Main"
        let (rr, rn) = (b.create_string("window"), b.create_string("Main"));
        let s_root = fb::PathStep::create(&mut b, &fb::PathStepArgs { role: Some(rr), name: Some(rn), index: 0 });
        let root_path = b.create_vector(&[s_root]);
        let root_ref = fb::ElementRef::create(&mut b, &fb::ElementRefArgs { object_name: None, path: Some(root_path) });
        let (rrole, rname) = (b.create_string("window"), b.create_string("Main"));
        let root = fb::UiNode::create(
            &mut b,
            &fb::UiNodeArgs {
                ref_: Some(root_ref),
                role: Some(rrole),
                name: Some(rname),
                children: Some(children),
                ..Default::default()
            },
        );
        let roots = b.create_vector(&[root]);
        let snap = fb::UiSnapshot::create(
            &mut b,
            &fb::UiSnapshotArgs {
                request_id: 7,
                format: fb::SnapshotFormat::Accessibility,
                roots: Some(roots),
                tree_hash: 0,
            },
        );
        b.finish(snap, None);

        let got = flatbuffers::root::<fb::UiSnapshot>(b.finished_data()).expect("valid UiSnapshot");
        assert_eq!(got.request_id(), 7);
        assert_eq!(got.format(), fb::SnapshotFormat::Accessibility);
        let root = got.roots().unwrap().get(0);
        assert_eq!(root.role().unwrap(), "window");
        let child = root.children().unwrap().get(0);
        assert_eq!(child.role().unwrap(), "button");
        assert_eq!(child.actions().unwrap().get(0), "Press");
        // the node's ref is the sender's ElementRef: its last step addresses the button.
        let path = child.ref_().unwrap().path().unwrap();
        assert_eq!(path.get(path.len() - 1).role().unwrap(), "button");
        assert_eq!(path.get(path.len() - 1).name().unwrap(), "OK");
    }

    #[test]
    fn machine_commands_and_events_roundtrip() {
        let bytes = send_machine_event(4, "indexing", "start", r#"{"mode":"all"}"#);
        let env = flatbuffers::root::<fb::CommandEnvelope>(&bytes).unwrap();
        assert_eq!(env.request_id(), 4);
        let c = env.command_as_send_machine_event().unwrap();
        assert_eq!(c.machine(), Some("indexing"));
        assert_eq!(c.event(), Some("start"));
        assert_eq!(c.args(), Some(r#"{"mode":"all"}"#));

        let bytes = get_machines(5, "");
        let env = flatbuffers::root::<fb::CommandEnvelope>(&bytes).unwrap();
        assert_eq!(env.command_as_get_machines().unwrap().machine(), None);

        let mut b = FlatBufferBuilder::new();
        let machine = b.create_string("question");
        let state = b.create_string("asking");
        let answer = b.create_string("answer");
        let events = b.create_vector(&[answer]);
        let context = b.create_string(r#"{"text":"Reindex?","options":["Reindex","Cancel"]}"#);
        let ms = fb::MachineState::create(
            &mut b,
            &fb::MachineStateArgs {
                machine: Some(machine),
                state: Some(state),
                events: Some(events),
                context: Some(context),
                revision: 3,
            },
        );
        let machines = b.create_vector(&[ms]);
        let states =
            fb::MachineStates::create(&mut b, &fb::MachineStatesArgs { request_id: 5, machines: Some(machines) });
        let env = fb::EventEnvelope::create(
            &mut b,
            &fb::EventEnvelopeArgs {
                seq: 1,
                timestamp_ms: 0,
                event_type: fb::Event::MachineStates,
                event: Some(states.as_union_value()),
                caused_by: 0,
            },
        );
        b.finish(env, None);
        match parse_event(b.finished_data()).unwrap().1 {
            Incoming::MachineStates { request_id, machines } => {
                assert_eq!(request_id, 5);
                assert_eq!(machines[0]["state"], "asking");
                assert_eq!(machines[0]["events"], json!(["answer"]));
                assert_eq!(machines[0]["context"]["options"][1], "Cancel");
            }
            other => panic!("expected MachineStates, got {other:?}"),
        }

        let mut b = FlatBufferBuilder::new();
        let args = fb::MachineTransitionArgs {
            machine: Some(b.create_string("indexing")),
            from: Some(b.create_string("indexing")),
            to: Some(b.create_string("finishing")),
            event: Some(b.create_string("interrupt")),
            source: Some(b.create_string("agent")),
            context: Some(b.create_string(r#"{"interrupted":true}"#)),
            revision: 7,
        };
        let t = fb::MachineTransition::create(&mut b, &args);
        let env = fb::EventEnvelope::create(
            &mut b,
            &fb::EventEnvelopeArgs {
                seq: 2,
                timestamp_ms: 0,
                event_type: fb::Event::MachineTransition,
                event: Some(t.as_union_value()),
                caused_by: 0,
            },
        );
        b.finish(env, None);
        let v = event_to_json(b.finished_data()).unwrap();
        assert_eq!(v["type"], "MachineTransition");
        assert_eq!(v["to"], "finishing");
        assert_eq!(v["source"], "agent");
        assert_eq!(v["context"]["interrupted"], true);
    }
}
