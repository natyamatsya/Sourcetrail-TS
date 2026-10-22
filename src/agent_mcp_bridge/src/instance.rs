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
use std::process::{Child, Command, Stdio};
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
    /// What the instance was started with (spawned instances only).
    launch: Option<Launch>,
}

/// Launch facts reported by `instance_list`.
struct Launch {
    bin: PathBuf,
    headless: bool,
    /// Where the app's stdout and stderr go (see [`InstanceManager::start`]).
    console_file: PathBuf,
    /// The Qt installation put on the app's PATH (see [`qt_prefix`]).
    qt: Option<PathBuf>,
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

    /// Exit status of a spawned instance that is gone (`None` while it runs, and
    /// for an attached one, whose process the bridge does not own).
    fn exit_status(&mut self) -> Option<std::process::ExitStatus> {
        self.child.as_mut()?.try_wait().ok().flatten()
    }

    fn describe(&mut self) -> Value {
        let exit = self.exit_status();
        let mut v = json!({
            "id": self.id,
            "spawned": self.is_spawned(),
            "pid": self.child.as_ref().map(Child::id),
            "running": exit.is_none(),
        });
        if let Some(status) = exit {
            v["exit_code"] = json!(status.code());
        }
        if let Some(l) = &self.launch {
            v["bin"] = json!(l.bin.display().to_string());
            v["headless"] = json!(l.headless);
            v["console_file"] = json!(l.console_file.display().to_string());
            v["qt"] = json!(l.qt.as_ref().map(|q| q.display().to_string()));
        }
        v
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
    /// Why the last `start` of an id failed. A later call on that id says so,
    /// instead of attaching to an app that never came up.
    failed_starts: HashMap<String, String>,
}

impl InstanceManager {
    pub fn new() -> Self {
        Self { instances: HashMap::new(), failed_starts: HashMap::new() }
    }

    /// Get the instance for `id`, lazily **attaching** to an already-running app if
    /// we have not seen it yet. This keeps the simple "just call a tool" UX: the
    /// default ("") instance attaches to a plain `Sourcetrail` with agent control.
    ///
    /// An empty `id` is the running app: the one running instance when the bridge
    /// knows exactly one, else the unnamespaced `st.agent.*` channels (see
    /// [`Self::resolve`]).
    pub fn get_or_attach(&mut self, id: &str) -> Result<&mut AppInstance> {
        let resolved = self.resolve(id);
        let id: &str = &resolved;
        if !self.instances.contains_key(id) {
            // Before connecting: the handshake with an absent app costs a round trip.
            if let Some(reason) = self.failed_starts.get(id) {
                bail!("instance '{id}' is not running: its instance_start failed: {reason}");
            }
            let bridge =
                Bridge::connect_instance(id).with_context(|| format!("attach to instance '{id}'"))?;
            // Connecting succeeds whether or not an app listens: only an app that
            // answered the handshake is an instance. Keeping the others would list
            // a phantom that every later call times out on.
            let handshake = bridge.handshake();
            if handshake["ok"].as_bool() != Some(true) {
                let error = handshake["error"].as_str().unwrap_or("no answer").to_string();
                if id.is_empty() {
                    let running = self.running_ids();
                    let hint = if running.is_empty() {
                        "Start one with instance_start, or launch Sourcetrail with --agent-instance <id> \
                         and address it with `instance: \"<id>\"`."
                            .to_string()
                    } else {
                        format!(
                            "{} instances are running, so `instance` is needed to say which: {}.",
                            running.len(),
                            running.iter().map(|i| format!("'{i}'")).collect::<Vec<_>>().join(", ")
                        )
                    };
                    bail!("no app is listening on the default agent channels ({error}). {hint}");
                }
                bail!(
                    "instance '{id}' is not running (no answer on its agent channels: {error}). \
                     Start it with instance_start, or launch Sourcetrail with --agent-instance {id}."
                );
            }
            self.instances
                .insert(id.to_string(), AppInstance { id: id.to_string(), child: None, bridge, launch: None });
        }
        let instance = self.instances.get_mut(id).unwrap();
        // A spawned instance that is gone: say so, rather than failing later with
        // "no receiver" on its channels.
        if let Some(status) = instance.exit_status() {
            let console = instance.launch.as_ref().map(|l| l.console_file.display().to_string());
            bail!(
                "instance '{id}' is not running (it exited: {status}); its console output is in {}",
                console.unwrap_or_default()
            );
        }
        Ok(instance)
    }

    /// The ids of the spawned instances that are still running, sorted.
    fn running_ids(&mut self) -> Vec<String> {
        let mut ids: Vec<String> = self
            .instances
            .iter_mut()
            .filter_map(|(id, i)| (i.is_spawned() && i.exit_status().is_none()).then(|| id.clone()))
            .collect();
        ids.sort();
        ids
    }

    /// The instance an `instance` argument addresses.
    ///
    /// Empty means "the running app". `instance_start` always namespaces its
    /// channels (`--agent-instance <id>`), so taking empty to mean only the
    /// unnamespaced channels would make the default unreachable for every app the
    /// bridge started. So when exactly one spawned instance runs, empty is that
    /// one; with none or several it stays the unnamespaced default, which is how
    /// an app started by hand is reached.
    fn resolve(&mut self, id: &str) -> String {
        if !id.is_empty() {
            return id.to_string();
        }
        let mut running = self.running_ids();
        if running.len() == 1 {
            running.remove(0)
        } else {
            String::new()
        }
    }

    /// Spawn a new app instance and connect to it. Returns its resolved id.
    pub fn start(&mut self, opts: StartOpts) -> Result<String> {
        if opts.bin.as_os_str().is_empty() {
            bail!("instance_start: `bin` (path to the Sourcetrail binary) is required");
        }
        // Checked here, not left to the spawn: with the working directory set to
        // the binary's, a missing binary fails as "the directory name is invalid".
        if !opts.bin.is_file() {
            bail!("instance_start: no Sourcetrail binary at {}", opts.bin.display());
        }
        let id = if opts.id.is_empty() {
            git_label(&opts.bin).unwrap_or_else(|| fallback_label(&opts.bin))
        } else {
            sanitize(&opts.id)
        };
        if self.instances.contains_key(&id) {
            bail!("instance '{id}' already exists (stop it first or pass a distinct id)");
        }
        match self.spawn(&id, opts) {
            Ok(()) => {
                self.failed_starts.remove(&id);
                Ok(id)
            }
            Err(e) => {
                self.failed_starts.insert(id, format!("{e:#}"));
                Err(e)
            }
        }
    }

    fn spawn(&mut self, id: &str, opts: StartOpts) -> Result<()> {
        // Agent control is always on in agent builds; only the namespace is passed.
        let mut cmd = Command::new(&opts.bin);
        cmd.arg("--agent-instance").arg(id);
        if opts.headless {
            cmd.env("QT_QPA_PLATFORM", "offscreen");
        }
        // Never let the child inherit our stdio: the MCP server speaks JSON-RPC on
        // stdout, and one console line from the app (the console logger writes to
        // std::cout) corrupts the protocol stream. Console output goes to a file
        // instead, so a crash's last words (qFatal, CRT asserts) survive.
        let console_file = new_run_path(id, "console.txt")?;
        let console = std::fs::File::create(&console_file)
            .with_context(|| format!("create console file {}", console_file.display()))?;
        cmd.stdin(Stdio::null())
            .stdout(console.try_clone().context("duplicate console file handle")?)
            .stderr(console);
        // Run from the executable's directory rather than the server's, so nothing
        // the app resolves against the working directory depends on where the MCP
        // client happened to start the bridge.
        if let Some(dir) = opts.bin.parent().filter(|d| !d.as_os_str().is_empty()) {
            cmd.current_dir(dir);
        }
        // Run on the Qt the binary was built with: its bin first on PATH (a dev
        // build is not deployed, so without it the loader fails before main()),
        // and its plugins on QT_PLUGIN_PATH (windeployqt copies only the
        // platform plugin of a desktop session, so an offscreen start of a
        // deployed build has no platform to load and aborts).
        let qt = qt_prefix(&opts.bin);
        if let Some(qt) = &qt {
            let sep = if cfg!(windows) { ";" } else { ":" };
            let path = std::env::var("PATH").unwrap_or_default();
            cmd.env("PATH", format!("{}{sep}{path}", qt.join("bin").display()));
            cmd.env("QT_PLUGIN_PATH", qt.join("plugins"));
        }
        let child = cmd
            .spawn()
            .with_context(|| format!("spawn {}", opts.bin.display()))?;
        let launch = Launch { bin: opts.bin.clone(), headless: opts.headless, console_file, qt };

        // Poll until the app answers the handshake (or it died / timed out).
        let mut instance = wait_for_ready(id, child, launch)?;

        // Optionally load a project right away.
        if let Some(project) = &opts.project {
            instance.bridge().load_project(project)?;
        }
        self.instances.insert(id.to_string(), instance);
        Ok(())
    }

    /// Kill (if spawned) and forget an instance. Returns the id it acted on, which
    /// an empty argument resolves to.
    pub fn kill(&mut self, id: &str) -> Result<String> {
        let id = self.resolve(id);
        if id.is_empty() && !self.instances.contains_key("") {
            bail!("`instance` is needed: no single running instance to default to (instance_list)");
        }
        // Drop kills the child.
        self.instances.remove(&id).ok_or_else(|| anyhow!("no such instance '{id}' (instance_list)"))?;
        Ok(id)
    }

    pub fn list(&mut self) -> Value {
        let mut items: Vec<Value> = self.instances.values_mut().map(AppInstance::describe).collect();
        items.extend(
            self.failed_starts
                .iter()
                .filter(|(id, _)| !self.instances.contains_key(*id))
                .map(|(id, reason)| json!({ "id": id, "running": false, "start_failed": reason })),
        );
        json!({ "instances": items })
    }
}

impl Default for InstanceManager {
    fn default() -> Self {
        Self::new()
    }
}

/// Spawn-side readiness: retry until the app can actually be *driven*, failing fast
/// if the child exits meanwhile.
///
/// A successful `Bridge::connect_instance` only means the SHM segments exist — the
/// app may not have bound its command receiver yet, so returning here let a
/// following `load_project` (or the caller's first command) race the channel and
/// fail. Instead we gate on the connect-time handshake (`GetInfo` -> `AppInfo`)
/// round-tripping: that proves the receiver is bound AND replying. This closes the
/// startup race, and a persistent handshake failure with a bound receiver surfaces
/// a version skew with an actionable message rather than a later opaque timeout.
fn wait_for_ready(id: &str, mut child: Child, launch: Launch) -> Result<AppInstance> {
    let deadline = Instant::now() + READY_TIMEOUT;
    let mut last_handshake = Value::Null;
    loop {
        if let Some(status) = child.try_wait().context("poll child")? {
            bail!(
                "app exited before its channels were ready ({status}{}); its console output is in {}",
                exit_hint(status),
                launch.console_file.display()
            );
        }
        if let Ok(bridge) = Bridge::connect_instance(id) {
            let hs = bridge.handshake();
            if hs.get("ok").and_then(Value::as_bool) == Some(true) {
                return Ok(AppInstance { id: id.to_string(), child: Some(child), bridge, launch: Some(launch) });
            }
            last_handshake = hs;
        }
        if Instant::now() >= deadline {
            let _ = child.kill();
            let _ = child.wait();
            // If a receiver was bound but the handshake never completed, it's almost
            // certainly a bridge/app version skew, not a slow start — say so.
            if last_handshake.get("skew").and_then(Value::as_str) == Some("unreachable") {
                bail!(
                    "instance '{id}' came up but is unreachable within {READY_TIMEOUT:?}: {}",
                    last_handshake.get("error").and_then(Value::as_str).unwrap_or("handshake failed")
                );
            }
            // The app kept running without answering. Offscreen, a modal dialog
            // during startup is invisible and blocks it.
            let hint = if launch.headless {
                "a startup dialog may be waiting invisibly; start it with headless: false to see it"
            } else {
                "check its window for a startup dialog"
            };
            bail!(
                "instance '{id}' did not become driveable within {READY_TIMEOUT:?} (no successful handshake): \
                 {hint}. Its console output is in {}",
                launch.console_file.display()
            );
        }
        std::thread::sleep(READY_POLL);
    }
}

/// What an exit status says that its number does not, for the ones a failed
/// start actually produces.
fn exit_hint(status: std::process::ExitStatus) -> &'static str {
    // STATUS_DLL_NOT_FOUND: the loader gave up before main(), so the app wrote
    // nothing to explain it. On a dev build that is Qt not being deployed.
    const STATUS_DLL_NOT_FOUND: u32 = 0xC000_0135;
    match status.code() {
        Some(code) if cfg!(windows) && code as u32 == STATUS_DLL_NOT_FOUND => {
            ": a DLL is missing, most likely Qt's: set SOURCETRAIL_MCP_QT_DIR to the Qt prefix the binary was built with"
        }
        _ => "",
    }
}

/// The Qt installation (its prefix: `bin/`, `plugins/`) a spawned app runs on:
/// `$SOURCETRAIL_MCP_QT_DIR` if set, else the Qt the binary was configured
/// with, read from `Qt6_DIR` in the CMakeCache.txt of the build tree it sits in
/// (`<prefix>/lib/cmake/Qt6`). `None` leaves the inherited environment alone,
/// e.g. for an installed, fully deployed Sourcetrail.
fn qt_prefix(bin: &Path) -> Option<PathBuf> {
    if let Some(dir) = std::env::var_os("SOURCETRAIL_MCP_QT_DIR") {
        return Some(PathBuf::from(dir));
    }
    let cache = bin.ancestors().skip(1).map(|d| d.join("CMakeCache.txt")).find(|f| f.is_file())?;
    qt_prefix_from_cache(&std::fs::read_to_string(cache).ok()?).filter(|p| p.join("plugins").is_dir())
}

/// The Qt prefix named by a CMakeCache's `Qt6_DIR` entry. Pure (tested).
fn qt_prefix_from_cache(cache: &str) -> Option<PathBuf> {
    let qt6_dir = cache.lines().find_map(|l| l.strip_prefix("Qt6_DIR:PATH="))?.trim();
    // <prefix>/lib/cmake/Qt6
    Some(Path::new(qt6_dir).parent()?.parent()?.parent()?.to_path_buf())
}

/// The bridge's data directory (console files, later logs and captures):
/// `$SOURCETRAIL_MCP_HOME`, else `sourcetrail-mcp` in the per-user local data
/// directory.
pub fn home() -> PathBuf {
    if let Some(home) = std::env::var_os("SOURCETRAIL_MCP_HOME") {
        return PathBuf::from(home);
    }
    std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("XDG_DATA_HOME").map(PathBuf::from))
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".local").join("share")))
        .unwrap_or_else(std::env::temp_dir)
        .join("sourcetrail-mcp")
}

/// A new file for a run of instance `id`: `<home>/logs/<id>-<unix ms>.<ext>`, so
/// every run keeps its own (before/after a change stay comparable).
fn new_run_path(id: &str, ext: &str) -> Result<PathBuf> {
    let dir = home().join("logs");
    std::fs::create_dir_all(&dir).with_context(|| format!("create log dir {}", dir.display()))?;
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or_default();
    Ok(dir.join(format!("{id}-{stamp}.{ext}")))
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_qt_prefix_comes_from_qt6_dir() {
        let cache = "CMAKE_BUILD_TYPE:STRING=Release\n\
                     Qt6_DIR:PATH=C:/Qt/Qt6.8.5/6.8.5/msvc2022_64/lib/cmake/Qt6\n\
                     Qt6Core_DIR:PATH=C:/elsewhere\n";
        assert_eq!(qt_prefix_from_cache(cache), Some(PathBuf::from("C:/Qt/Qt6.8.5/6.8.5/msvc2022_64")));
        assert_eq!(qt_prefix_from_cache("CMAKE_BUILD_TYPE:STRING=Release\n"), None);
    }
}
