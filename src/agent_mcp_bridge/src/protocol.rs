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
        // IndexingStarted (empty) and any future arms: type tag only.
        _ => {}
    }
    Ok(obj)
}

// --- UiState decoding (st.agent.state) --------------------------------------

/// Decode a `UiStateEnvelope`; returns `(request_id, ui_state_json)`.
pub fn parse_ui_state(bytes: &[u8]) -> Result<(u64, Value)> {
    let env = flatbuffers::root::<fb::UiStateEnvelope>(bytes)
        .map_err(|e| anyhow!("bad UiStateEnvelope: {e}"))?;
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
        "indexing_active": s.indexing_active(),
        "error_total": s.error_total(),
        "error_fatal": s.error_fatal(),
        "active_nodes": active_nodes,
        "graph": graph,
        "code_view": code_view,
        "search_matches": search_matches,
        "tabs": tabs,
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
        assert_eq!(json["error_total"], 2);
        assert_eq!(json["active_nodes"][0]["serialized_name"], "m::Foo");
        assert_eq!(json["tabs"][0]["tab_id"], 10);
        assert_eq!(json["tabs"][0]["active"], true);
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
}
