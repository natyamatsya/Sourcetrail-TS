//! The bridge's own view of the app's state machines, kept from what the app
//! sends anyway: the `MachineStates` it answers (probes, `machine_state`,
//! `ui_state`) and the `MachineTransition` events it publishes as machines move.
//! Those events go out on the thread that moves the machine, not through the
//! command reader, so the view stays current while the app does not answer
//! commands. That is when it is reported: a "not answering" error carries it
//! as a tree, the way `top` shows processes, so the agent sees what the app is
//! busy with.

use std::collections::BTreeMap;
use std::time::{SystemTime, UNIX_EPOCH};

use serde_json::{json, Map, Value};

/// Milliseconds since the epoch, the clock the app stamps its events with.
pub fn now_ms() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_millis() as u64).unwrap_or(0)
}

#[derive(Debug, Clone)]
struct Entry {
    state: String,
    context: Value,
    revision: u64,
    // When the machine entered `state`, by the app's clock, and the move that
    // did it; unknown for a state first learned from a snapshot.
    entered_ms: Option<u64>,
    entered_by: Option<Value>,
    // When the bridge first saw it in `state` (a lower bound for how long).
    seen_ms: u64,
}

impl Entry {
    /// How long the machine has been in its state at `now_ms`, and whether that
    /// is only a lower bound (the state was first seen in a snapshot).
    fn duration(&self, now_ms: u64) -> (u64, bool) {
        match self.entered_ms {
            Some(at) => (now_ms.saturating_sub(at), false),
            None => (now_ms.saturating_sub(self.seen_ms), true),
        }
    }
}

#[derive(Debug, Default)]
pub struct MachineView {
    machines: BTreeMap<String, Entry>,
}

/// The machine a machine's work belongs to; `None` for the root.
fn parent(machine: &str) -> Option<&'static str> {
    match machine {
        "app" => None,
        "indexing" | "activation" | "search" | "bookmark" => Some("project"),
        _ => Some("app"), // project, question, project_wizard, preferences, and any newer one
    }
}

/// Whether `state` is the machine at rest: nothing in progress. The app's own
/// resting states (a spec's `resting`, else its initial state; see
/// ConcurrentStateMachine.h), except that the app machine rests running, not
/// starting.
fn at_rest(machine: &str, state: &str) -> bool {
    match machine {
        "app" => state == "running",
        "project" => state == "ready" || state == "no_project",
        "project_wizard" | "preferences" => state == "closed",
        _ => state == "idle",
    }
}

impl MachineView {
    /// A `MachineStates` reply (each element as `protocol::machine_state_json`
    /// builds it), received at `at_ms`. A machine whose state changed since the
    /// view last saw it entered the new one at an unknown time.
    pub fn note_states(&mut self, machines: &[Value], at_ms: u64) {
        for m in machines {
            let Some(name) = m["machine"].as_str() else { continue };
            let state = m["state"].as_str().unwrap_or_default();
            let revision = m["revision"].as_u64().unwrap_or(0);
            match self.machines.get_mut(name) {
                Some(e) if e.state == state => {
                    e.context = m["context"].clone();
                    e.revision = e.revision.max(revision);
                }
                Some(e) if e.revision > revision => {} // older than what the view has
                _ => {
                    self.machines.insert(
                        name.to_string(),
                        Entry {
                            state: state.to_string(),
                            context: m["context"].clone(),
                            revision,
                            entered_ms: None,
                            entered_by: None,
                            seen_ms: at_ms,
                        },
                    );
                }
            }
        }
    }

    /// A `MachineTransition` (as `protocol::machine_transition_json` builds it),
    /// stamped `at_ms` by the app.
    pub fn note_transition(&mut self, t: &Value, at_ms: u64) {
        let Some(name) = t["machine"].as_str() else { return };
        let revision = t["revision"].as_u64().unwrap_or(0);
        if self.machines.get(name).is_some_and(|e| e.revision > revision) {
            return; // a snapshot already showed a later move
        }
        let to = t["to"].as_str().unwrap_or_default();
        let entered_by = json!({
            "from": t["from"],
            "event": t["event"],
            "source": t["source"],
        });
        let entry = self.machines.entry(name.to_string()).or_insert_with(|| Entry {
            state: String::new(),
            context: json!({}),
            revision: 0,
            entered_ms: None,
            entered_by: None,
            seen_ms: at_ms,
        });
        if entry.state != to || entry.entered_ms.is_none() {
            entry.entered_ms = Some(at_ms);
            entry.entered_by = Some(entered_by);
            entry.seen_ms = at_ms;
        }
        entry.state = to.to_string();
        entry.context = t["context"].clone();
        entry.revision = revision;
    }

    pub fn is_empty(&self) -> bool {
        self.machines.is_empty()
    }

    /// The machines as a tree at `now_ms`: each node `{ machine, state, busy,
    /// for_ms, entered_by?, context, children }`, `for_ms` a lower bound
    /// (`for_ms_at_least`) when the view does not know when the state began.
    pub fn tree(&self, now_ms: u64) -> Vec<Value> {
        let mut roots = Vec::new();
        for name in self.machines.keys() {
            let has_parent = parent(name).is_some_and(|p| self.machines.contains_key(p));
            if !has_parent {
                roots.push(self.node(name, now_ms));
            }
        }
        roots
    }

    fn node(&self, name: &str, now_ms: u64) -> Value {
        let e = &self.machines[name];
        let mut node = Map::new();
        node.insert("machine".into(), json!(name));
        node.insert("state".into(), json!(e.state));
        node.insert("busy".into(), json!(!at_rest(name, &e.state)));
        let (for_ms, at_least) = e.duration(now_ms);
        node.insert("for_ms".into(), json!(for_ms));
        if at_least {
            node.insert("for_ms_at_least".into(), json!(true));
        }
        if let Some(by) = &e.entered_by {
            node.insert("entered_by".into(), by.clone());
        }
        if e.context.as_object().is_some_and(|c| !c.is_empty()) {
            node.insert("context".into(), e.context.clone());
        }
        let children: Vec<Value> = self
            .machines
            .keys()
            .filter(|child| parent(child) == Some(name))
            .map(|child| self.node(child, now_ms))
            .collect();
        node.insert("children".into(), json!(children));
        Value::Object(node)
    }

    /// The busy machines, longest first: `{ machine, state, for_ms,
    /// for_ms_at_least? }`.
    pub fn in_progress(&self, now_ms: u64) -> Vec<Value> {
        let mut busy: Vec<(u64, Value)> = self
            .machines
            .iter()
            .filter(|(name, e)| !at_rest(name, &e.state))
            .map(|(name, e)| {
                let (for_ms, at_least) = e.duration(now_ms);
                let mut item = json!({ "machine": name, "state": e.state, "for_ms": for_ms });
                if at_least {
                    item["for_ms_at_least"] = json!(true);
                }
                (for_ms, item)
            })
            .collect();
        busy.sort_by(|a, b| b.0.cmp(&a.0));
        busy.into_iter().map(|(_, item)| item).collect()
    }

    /// `in_progress` as text for a message, one entry per busy machine:
    /// "indexing pending 3.4 s".
    pub fn summary(&self, now_ms: u64) -> Vec<String> {
        self.in_progress(now_ms)
            .iter()
            .map(|item| {
                let at_least = if item["for_ms_at_least"] == true { ">= " } else { "" };
                let seconds = item["for_ms"].as_u64().unwrap_or(0) as f64 / 1000.0;
                format!("{} {} {at_least}{seconds:.1} s", item["machine"].as_str().unwrap_or_default(),
                        item["state"].as_str().unwrap_or_default())
            })
            .collect()
    }

    /// The report a "not answering" error carries: `{ app_silent_ms, in_progress,
    /// machines, view }`.
    pub fn report(&self, silent_ms: u64, now_ms: u64) -> Value {
        json!({
            "app_silent_ms": silent_ms,
            "in_progress": self.in_progress(now_ms),
            "machines": self.tree(now_ms),
            "view": "the bridge's: the app's last machine states, and the transitions it published since",
        })
    }
}

/// The transitions of a `machine_send` / `machine_state` reply, their contexts compacted. The
/// reply's `machine` carries the machine's whole context as it is now, so a transition of that
/// machine keeps only the context fields that differ from it (a field the machine no longer
/// has is null): the project wizard's context, with its choices of 92 targets, was repeated in
/// every transition, 25-30k tokens a call. Other machines' transitions (a question asked
/// meanwhile, the project reopening) keep their contexts.
pub fn compact_transitions(snapshot: &Value, transitions: Vec<Value>) -> Vec<Value> {
    let machine = &snapshot["machine"];
    let now = &snapshot["context"];
    transitions
        .into_iter()
        .map(|mut transition| {
            if machine.is_null() || transition["machine"] != *machine {
                return transition;
            }
            let compacted = match (transition["context"].as_object(), now.as_object()) {
                (Some(then), Some(now)) => {
                    let mut differing = Map::new();
                    for (key, value) in then {
                        if now.get(key) != Some(value) {
                            differing.insert(key.clone(), value.clone());
                        }
                    }
                    for key in now.keys() {
                        if !then.contains_key(key) {
                            differing.insert(key.clone(), Value::Null);
                        }
                    }
                    Value::Object(differing)
                }
                _ if transition["context"] == *now => json!({}),
                _ => return transition,
            };
            transition["context"] = compacted;
            transition
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_transition_of_the_replying_machine_keeps_only_what_differs_from_it_now() {
        let choices = json!({ "cmake_target_glob": [{ "value": "leveldb" }, { "value": "leveldbutil" }] });
        let snapshot = json!({ "machine": "project_wizard", "state": "editing",
            "context": { "page": "source_group", "fields": { "name": "b" }, "choices": choices } });
        let transitions = vec![
            json!({ "machine": "project_wizard", "from": "editing", "to": "applying", "event": "set",
                    "context": { "applying": "set", "page": "source_group", "fields": { "name": "a" },
                                 "choices": choices } }),
            json!({ "machine": "project_wizard", "from": "applying", "to": "editing", "event": "applied",
                    "context": { "page": "source_group", "fields": { "name": "b" }, "choices": choices } }),
            json!({ "machine": "question", "from": "idle", "to": "asking", "event": "ask",
                    "context": { "id": 1, "text": "Please enter a project name.", "options": ["OK"] } }),
        ];

        let compacted = compact_transitions(&snapshot, transitions);

        assert_eq!(compacted[0]["context"], json!({ "applying": "set", "fields": { "name": "a" } }));
        assert_eq!(compacted[1]["context"], json!({}));
        assert_eq!(compacted[0]["event"], "set");
        // Another machine's transition keeps its context: the question's text is the point.
        assert_eq!(compacted[2]["context"]["text"], "Please enter a project name.");
    }

    #[test]
    fn a_field_the_machine_has_gained_since_reads_null_in_the_transition() {
        let snapshot = json!({ "machine": "indexing", "state": "idle", "context": { "outcome": "kept" } });
        let transitions = vec![json!({ "machine": "indexing", "to": "finishing", "context": { "progress": 3 } })];

        let compacted = compact_transitions(&snapshot, transitions);

        assert_eq!(compacted[0]["context"], json!({ "progress": 3, "outcome": null }));
    }

    fn state(machine: &str, state: &str, revision: u64) -> Value {
        json!({ "machine": machine, "state": state, "events": [], "context": {}, "revision": revision })
    }

    fn transition(machine: &str, from: &str, to: &str, event: &str, revision: u64) -> Value {
        json!({ "machine": machine, "from": from, "to": to, "event": event, "source": "app",
                "context": { "mode": "all" }, "revision": revision })
    }

    #[test]
    fn a_snapshot_then_a_transition_shows_what_is_in_progress_and_for_how_long() {
        let mut view = MachineView::default();
        view.note_states(
            &[state("app", "running", 1), state("project", "ready", 4), state("indexing", "idle", 2)],
            1_000,
        );
        view.note_transition(&transition("indexing", "idle", "pending", "requested", 3), 2_000);

        let tree = view.tree(5_400);
        assert_eq!(tree.len(), 1);
        let app = &tree[0];
        assert_eq!(app["machine"], "app");
        assert_eq!(app["busy"], false);
        let project = &app["children"][0];
        assert_eq!(project["machine"], "project");
        let indexing = &project["children"][0];
        assert_eq!(indexing["state"], "pending");
        assert_eq!(indexing["busy"], true);
        assert_eq!(indexing["for_ms"], 3_400);
        assert_eq!(indexing["entered_by"]["event"], "requested");
        assert_eq!(indexing["context"]["mode"], "all");
        assert!(indexing.get("for_ms_at_least").is_none());
        assert_eq!(
            view.in_progress(5_400),
            vec![json!({ "machine": "indexing", "state": "pending", "for_ms": 3_400 })]
        );
        assert_eq!(view.summary(5_400), vec!["indexing pending 3.4 s".to_string()]);
    }

    #[test]
    fn a_state_first_seen_in_a_snapshot_has_a_lower_bound_for_how_long() {
        let mut view = MachineView::default();
        view.note_states(&[state("question", "asking", 7)], 10_000);
        let tree = view.tree(12_000);
        assert_eq!(tree[0]["machine"], "question");
        assert_eq!(tree[0]["for_ms"], 2_000);
        assert_eq!(tree[0]["for_ms_at_least"], true);
        assert_eq!(
            view.in_progress(12_000),
            vec![json!({ "machine": "question", "state": "asking", "for_ms": 2_000, "for_ms_at_least": true })]
        );
        assert_eq!(view.summary(12_000), vec!["question asking >= 2.0 s".to_string()]);
    }

    #[test]
    fn an_older_snapshot_does_not_undo_a_later_transition() {
        let mut view = MachineView::default();
        view.note_transition(&transition("project", "ready", "loading", "reloading", 9), 1_000);
        view.note_states(&[state("project", "ready", 8)], 1_500);
        assert_eq!(view.tree(2_000)[0]["state"], "loading");
    }

    #[test]
    fn each_machine_rests_in_its_own_states() {
        let mut view = MachineView::default();
        view.note_states(
            &[
                state("app", "running", 1),
                state("project", "no_project", 1),
                state("project_wizard", "closed", 1),
                state("question", "idle", 1),
            ],
            1_000,
        );
        assert!(view.in_progress(2_000).is_empty());
        view.note_transition(&transition("project_wizard", "closed", "editing", "open", 2), 2_000);
        assert_eq!(view.summary(2_500), vec!["project_wizard editing 0.5 s".to_string()]);
    }

    #[test]
    fn busy_machines_are_listed_longest_first() {
        let mut view = MachineView::default();
        view.note_transition(&transition("project", "ready", "loading", "reloading", 5), 1_000);
        view.note_transition(&transition("indexing", "finishing", "idle", "end", 9), 1_000);
        view.note_transition(&transition("activation", "idle", "activating", "requested", 2), 3_000);
        assert_eq!(
            view.summary(4_000),
            vec!["project loading 3.0 s".to_string(), "activation activating 1.0 s".to_string()]
        );
    }
}
