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

pub fn get_ui_state(request_id: u64) -> Vec<u8> {
    let mut b = FlatBufferBuilder::new();
    let c = fb::GetUiState::create(&mut b, &fb::GetUiStateArgs {});
    finish(b, request_id, fb::Command::GetUiState, c.as_union_value())
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

// --- Event decoding (st.agent.events) ---------------------------------------

/// A decoded event relevant to correlation / observation.
#[derive(Debug, Clone)]
pub enum Incoming {
    CommandResult { request_id: u64, ok: bool, message: String },
    SearchCompleted(Value),
    AppStateChanged(i8),
    TabsChanged(Value),
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
            &fb::UiSnapshotArgs { request_id: 7, format: fb::SnapshotFormat::Accessibility, roots: Some(roots) },
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
