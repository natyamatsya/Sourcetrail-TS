//! App lifecycle + multi-instance management.
//!
//! [`InstanceManager`] owns a pool of Sourcetrail instances, each namespaced by an
//! `id` (which maps to `st.agent.<id>.*` channels). An instance is either
//! **spawned** by the bridge (the manager owns the child process and can kill it)
//! or **attached** to an already-running app. Multiple instances run side by side
//! for comparison tests (e.g. baseline vs candidate), each with its own [`Bridge`].
//!
//! Instance ids default to a git label of the binary's checkout
//! (`<branch>-<shorthash>`), so pointing the bridge at a checkout self-identifies
//! the version under test in the channel namespace.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::{Child, Command};
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Context, Result};
use serde_json::{json, Value};

use crate::ipc::Bridge;

/// How long to wait for a freshly spawned app to open its channels.
const READY_TIMEOUT: Duration = Duration::from_secs(30);
const READY_POLL: Duration = Duration::from_millis(250);

/// Options for spawning an instance.
#[derive(Debug, Default, Clone)]
pub struct StartOpts {
    /// Path to the `Sourcetrail` binary to launch.
    pub bin: PathBuf,
    /// Explicit instance id; if empty, derived from the binary's git checkout.
    pub id: String,
    /// Project file to load once the app is up (optional).
    pub project: Option<String>,
    /// Run offscreen (`QT_QPA_PLATFORM=offscreen`); default true for tests.
    pub headless: bool,
}

/// A managed app + its control channel.
pub struct AppInstance {
    id: String,
    /// `None` when we attached to an already-running app (we don't own it).
    child: Option<Child>,
    bridge: Bridge,
}

impl AppInstance {
    pub fn id(&self) -> &str {
        &self.id
    }
    pub fn is_spawned(&self) -> bool {
        self.child.is_some()
    }
    pub fn bridge(&mut self) -> &mut Bridge {
        &mut self.bridge
    }
}

impl Drop for AppInstance {
    fn drop(&mut self) {
        if let Some(mut child) = self.child.take() {
            let _ = child.kill();
            let _ = child.wait();
        }
    }
}

pub struct InstanceManager {
    instances: HashMap<String, AppInstance>,
}

impl InstanceManager {
    pub fn new() -> Self {
        Self { instances: HashMap::new() }
    }

    /// Get the instance for `id`, lazily **attaching** to an already-running app if
    /// we have not seen it yet. This keeps the simple "just call a tool" UX: the
    /// default ("") instance attaches to a plain `Sourcetrail --agent-control`.
    pub fn get_or_attach(&mut self, id: &str) -> Result<&mut AppInstance> {
        if !self.instances.contains_key(id) {
            let bridge = Bridge::connect_instance(id)
                .with_context(|| format!("attach to instance '{id}'"))?;
            self.instances.insert(
                id.to_string(),
                AppInstance { id: id.to_string(), child: None, bridge },
            );
        }
        Ok(self.instances.get_mut(id).unwrap())
    }

    /// Spawn a new app instance and connect to it. Returns its resolved id.
    pub fn start(&mut self, opts: StartOpts) -> Result<String> {
        if opts.bin.as_os_str().is_empty() {
            bail!("start_instance: `bin` (path to the Sourcetrail binary) is required");
        }
        let id = if opts.id.is_empty() {
            git_label(&opts.bin).unwrap_or_else(|| fallback_label(&opts.bin))
        } else {
            sanitize(&opts.id)
        };
        if self.instances.contains_key(&id) {
            bail!("instance '{id}' already exists (kill it first or pass a distinct id)");
        }

        // Agent control is always on in agent builds; only the namespace is passed.
        let mut cmd = Command::new(&opts.bin);
        cmd.arg("--agent-instance").arg(&id);
        if opts.headless {
            cmd.env("QT_QPA_PLATFORM", "offscreen");
        }
        let child = cmd
            .spawn()
            .with_context(|| format!("spawn {}", opts.bin.display()))?;

        // Poll until the app has opened its channels (or it died / timed out).
        let mut instance = wait_for_ready(&id, child)?;

        // Optionally load a project right away.
        if let Some(project) = &opts.project {
            instance.bridge().load_project(project)?;
        }
        self.instances.insert(id.clone(), instance);
        Ok(id)
    }

    /// Kill (if spawned) and forget an instance.
    pub fn kill(&mut self, id: &str) -> Result<()> {
        self.instances
            .remove(id)
            .ok_or_else(|| anyhow!("no such instance '{id}'"))?; // Drop kills the child
        Ok(())
    }

    pub fn list(&self) -> Value {
        let items: Vec<Value> = self
            .instances
            .values()
            .map(|i| json!({ "id": i.id, "spawned": i.is_spawned() }))
            .collect();
        json!({ "instances": items })
    }
}

impl Default for InstanceManager {
    fn default() -> Self {
        Self::new()
    }
}

/// Spawn-side readiness: retry the connect until the app's channels exist, failing
/// fast if the child process exits meanwhile.
fn wait_for_ready(id: &str, mut child: Child) -> Result<AppInstance> {
    let deadline = Instant::now() + READY_TIMEOUT;
    loop {
        if let Some(status) = child.try_wait().context("poll child")? {
            bail!("app exited before its channels were ready (status {status})");
        }
        match Bridge::connect_instance(id) {
            Ok(bridge) => {
                return Ok(AppInstance { id: id.to_string(), child: Some(child), bridge });
            }
            Err(_) if Instant::now() < deadline => std::thread::sleep(READY_POLL),
            Err(e) => {
                let _ = child.kill();
                return Err(e.context(format!("instance '{id}' not ready within {READY_TIMEOUT:?}")));
            }
        }
    }
}

/// `<branch>-<shorthash>` for the binary's enclosing git checkout, sanitized for a
/// channel name — so pointing at a checkout self-labels the version under test.
pub fn git_label(bin: &Path) -> Option<String> {
    let dir = bin.parent()?;
    let short = git(dir, &["rev-parse", "--short", "HEAD"])?;
    let branch = git(dir, &["rev-parse", "--abbrev-ref", "HEAD"]).unwrap_or_else(|| "detached".into());
    Some(sanitize(&format!("{branch}-{short}")))
}

fn git(dir: &Path, args: &[&str]) -> Option<String> {
    let out = Command::new("git").arg("-C").arg(dir).args(args).output().ok()?;
    if !out.status.success() {
        return None;
    }
    let s = String::from_utf8_lossy(&out.stdout).trim().to_string();
    (!s.is_empty()).then_some(s)
}

fn fallback_label(bin: &Path) -> String {
    sanitize(
        bin.parent()
            .and_then(|p| p.file_name())
            .and_then(|n| n.to_str())
            .unwrap_or("app"),
    )
}

/// Keep channel-name-safe characters only (alnum, '-', '_'); everything else '-'.
fn sanitize(s: &str) -> String {
    s.chars()
        .map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' { c } else { '-' })
        .collect()
}
