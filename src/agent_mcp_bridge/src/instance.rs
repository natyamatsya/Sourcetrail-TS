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

use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Context, Result};
use serde_json::{json, Value};

use crate::debugger::{self, DebugSession};
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
    /// Settings profile: a name for an isolated user-data directory that persists
    /// across runs (empty = [`DEFAULT_PROFILE`]), or [`USER_PROFILE`] for the
    /// developer's own settings.
    pub profile: String,
    /// Run on a throwaway copy of the profile, removed when the instance goes: a
    /// clean start that keeps what the profile holds, and changes that never persist.
    pub fresh: bool,
    /// Attach the platform debugger once the app is up (crash dumps, live
    /// debugging); `None` = whenever one is installed.
    pub debugger: Option<bool>,
    /// Keep the app's animations; off by default (--no-animations), so a frame
    /// taken once a command settles shows the end state, not a transition.
    pub animations: bool,
}

/// The profile that means "the developer's own settings": no --user-data-dir.
pub const USER_PROFILE: &str = "user";
/// The profile spawned instances use when none is named.
pub const DEFAULT_PROFILE: &str = "agent";

/// A managed app + its control channel.
pub struct AppInstance {
    id: String,
    /// `None` when we attached to an already-running app (we don't own it).
    child: Option<Child>,
    bridge: Bridge,
    /// What the instance was started with (spawned instances only).
    launch: Option<Launch>,
    /// The debugger attached to it, if any (see [`InstanceManager::debug`]).
    debug: Option<DebugSession>,
}

/// Launch facts reported by `instance_list`.
struct Launch {
    bin: PathBuf,
    headless: bool,
    /// The run's files share this path without extension: `.console.txt` (the
    /// app's stdout and stderr), and with a debugger `.dmp`, `.cdb.log`, ...
    stem: PathBuf,
    /// The Qt installation put on the app's PATH (see [`qt_prefix`]).
    qt: Option<PathBuf>,
    /// The user-data directory passed to the app (--user-data-dir), if isolated.
    user_data_dir: Option<PathBuf>,
    /// `user_data_dir` is a throwaway copy, deleted on drop.
    fresh_copy: bool,
}

impl Launch {
    fn console_file(&self) -> PathBuf {
        with_suffix(&self.stem, ".console.txt")
    }
}

impl Drop for Launch {
    fn drop(&mut self) {
        if let (true, Some(dir)) = (self.fresh_copy, &self.user_data_dir) {
            let _ = std::fs::remove_dir_all(dir);
        }
    }
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

    /// Exit code of a spawned instance that is gone (`None` while it runs, held
    /// at a fault by its debugger included, and for an attached one, whose
    /// process the bridge does not own).
    fn exit_code(&mut self) -> Option<i32> {
        debugger::child_exit(self.child.as_mut()?, self.debug.as_ref())
    }

    fn describe(&mut self) -> Value {
        let exit = self.exit_code();
        let mut v = json!({
            "id": self.id,
            "spawned": self.is_spawned(),
            "pid": self.child.as_ref().map(Child::id),
            "running": exit.is_none(),
        });
        if let Some(code) = exit {
            v["exit_code"] = json!(format!("0x{:08x}", code as u32));
        }
        if let Some(d) = &self.debug {
            if let (Value::Object(m), Value::Object(dbg)) = (&mut v, d.describe()) {
                m.extend(dbg);
            }
            if exit.is_none() {
                v["stopped"] = json!(d.stop_status());
            }
        }
        if let Some(l) = &self.launch {
            v["bin"] = json!(l.bin.display().to_string());
            v["headless"] = json!(l.headless);
            v["console_file"] = json!(l.console_file().display().to_string());
            v["qt"] = json!(l.qt.as_ref().map(|q| q.display().to_string()));
            v["user_data_dir"] = json!(l.user_data_dir.as_ref().map(|d| d.display().to_string()));
            v["fresh"] = json!(l.fresh_copy);
        }
        v
    }
}

impl Drop for AppInstance {
    fn drop(&mut self) {
        if let Some(mut child) = self.child.take() {
            let _ = child.kill();
            // A process its debugger holds (at a fault, or just attached) cannot be
            // reaped until the debugger lets go: on Windows the wait blocks for
            // good. So end the debugger first, then wait, bounded.
            self.debug.take();
            let deadline = Instant::now() + Duration::from_secs(5);
            while !matches!(child.try_wait(), Ok(Some(_))) && Instant::now() < deadline {
                std::thread::sleep(Duration::from_millis(20));
            }
        }
        // `launch` drops after this body, so a fresh copy of a profile is removed
        // only once the app that used it is gone.
    }
}

pub struct InstanceManager {
    instances: HashMap<String, AppInstance>,
    /// Why the last `start` of an id failed. A later call on that id says so,
    /// instead of attaching to an app that never came up.
    failed_starts: HashMap<String, String>,
    /// Dumps open post-mortem (`instance_debug_dump`), by id.
    dumps: HashMap<String, DebugSession>,
}

impl InstanceManager {
    pub fn new() -> Self {
        Self { instances: HashMap::new(), failed_starts: HashMap::new(), dumps: HashMap::new() }
    }

    /// Get the instance for `id`, lazily **attaching** to an already-running app if
    /// we have not seen it yet. This keeps the simple "just call a tool" UX: the
    /// default ("") instance attaches to a plain `Sourcetrail` with agent control.
    ///
    /// An empty `id` is the running app: the one running instance when the bridge
    /// knows exactly one, else the unnamespaced `st.agent.*` channels (see
    /// [`Self::resolve`]).
    ///
    /// An instance stopped in its debugger answers nothing, so this fails at once
    /// with the stop instead of letting the call time out.
    pub fn get_or_attach(&mut self, id: &str) -> Result<&mut AppInstance> {
        let instance = self.lookup(id)?;
        if let Some(session) = &instance.debug {
            if let Some(stopped) = session.stop_status() {
                // instance.id, not the argument: an empty one resolved to this.
                bail!(
                    "instance '{}' is stopped in the debugger ({stopped}) and answers nothing until it runs: \
                     inspect it with instance_debug_command, resume it with the command \"{}\"",
                    instance.id,
                    session.resume_command()
                );
            }
        }
        Ok(instance)
    }

    /// [`Self::get_or_attach`] without the stopped-in-the-debugger check: for the
    /// debugger tools, which work on a stopped instance.
    fn lookup(&mut self, id: &str) -> Result<&mut AppInstance> {
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
            self.instances.insert(
                id.to_string(),
                AppInstance { id: id.to_string(), child: None, bridge, launch: None, debug: None },
            );
        }
        let instance = self.instances.get_mut(id).unwrap();
        // A spawned instance that is gone: say so (and where its crash dump is),
        // rather than failing later with "no receiver" on its channels.
        if let Some(code) = instance.exit_code() {
            let console = instance.launch.as_ref().map(|l| l.console_file().display().to_string());
            let dump = instance.debug.as_ref().map(DebugSession::dump_path).filter(|d| d.is_file());
            let dump = dump.map_or(String::new(), |d| format!("; crash dump: {}", d.display()));
            bail!(
                "instance '{id}' is not running (exit code 0x{:08x}{dump}); its console output is in {}",
                code as u32,
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
            .filter_map(|(id, i)| (i.is_spawned() && i.exit_code().is_none()).then(|| id.clone()))
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
        self.prune_dumps();
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
        if !opts.animations {
            cmd.arg("--no-animations");
        }
        if opts.headless {
            cmd.env("QT_QPA_PLATFORM", "offscreen");
        }
        // Never let the child inherit our stdio: the MCP server speaks JSON-RPC on
        // stdout, and one console line from the app (the console logger writes to
        // std::cout) corrupts the protocol stream. Console output goes to a file
        // instead, so a crash's last words (qFatal, CRT asserts) survive.
        let stem = new_run_stem(id)?;
        let console_file = with_suffix(&stem, ".console.txt");
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
        // Settings, logs and recent projects in an isolated profile, so an agent's
        // runs neither read nor change the developer's own. The app creates a
        // profile directory that does not exist yet, with the default settings.
        let (user_data_dir, fresh_copy) = user_data_dir_for(&opts.profile, opts.fresh, id)?;
        if let Some(dir) = &user_data_dir {
            cmd.arg("--user-data-dir").arg(dir);
        }
        let child = cmd
            .spawn()
            .with_context(|| format!("spawn {}", opts.bin.display()))?;
        let launch = Launch { bin: opts.bin.clone(), headless: opts.headless, stem, qt, user_data_dir, fresh_copy };

        // Poll until the app answers the handshake (or it died / timed out).
        let mut instance = wait_for_ready(id, child, launch)?;

        // The debugger attaches once the app is up (see debugger.rs for why not at
        // launch). Its files go beside the instance's console file.
        if opts.debugger.unwrap_or_else(debugger::available) {
            let pid = instance.child.as_ref().map(Child::id).unwrap_or_default();
            let stem = instance.launch.as_ref().map(|l| l.stem.clone()).unwrap_or_default();
            instance.debug = Some(
                debugger::attach(pid, id, &stem).context("attach the debugger (pass debugger: false to skip)")?,
            );
        }

        // Optionally load a project right away.
        if let Some(project) = &opts.project {
            instance.bridge().load_project(project)?;
        }
        self.instances.insert(id.to_string(), instance);
        Ok(())
    }

    /// Kill (if spawned) and forget an instance, or close an open dump. Returns
    /// the id it acted on, which an empty argument resolves to.
    pub fn kill(&mut self, id: &str) -> Result<String> {
        // One open dump is as unambiguous a default as one running instance.
        let id = match self.resolve(id) {
            r if !r.is_empty() => r,
            _ if self.dumps.len() == 1 && !self.instances.contains_key("") => {
                self.dumps.keys().next().cloned().unwrap_or_default()
            }
            r => r,
        };
        if id.is_empty() && !self.instances.contains_key("") {
            bail!("`instance` is needed: no single running instance or open dump to default to (instance_list)");
        }
        // Drop kills the child (and ends the debugger first).
        if self.instances.remove(&id).is_none() && self.dumps.remove(&id).is_none() {
            bail!("no such instance '{id}' (instance_list)");
        }
        Ok(id)
    }

    pub fn list(&mut self) -> Value {
        let mut items: Vec<Value> = self.instances.values_mut().map(AppInstance::describe).collect();
        items.extend(self.dumps.iter().map(|(id, session)| describe_dump(id, session)));
        items.extend(
            self.failed_starts
                .iter()
                .filter(|(id, _)| !self.instances.contains_key(*id))
                .map(|(id, reason)| json!({ "id": id, "running": false, "start_failed": reason })),
        );
        json!({ "instances": items })
    }

    /// Attach the platform debugger to an instance (if none is attached yet) and
    /// describe the session. A spawned instance is found by id; for an app the
    /// bridge attached to, pass its `pid`. `break_in` stops it right away.
    pub fn debug(&mut self, id: &str, pid: Option<u32>, break_in: bool) -> Result<Value> {
        let instance = self.lookup(id)?;
        if instance.debug.is_none() {
            let pid = instance
                .child
                .as_ref()
                .map(Child::id)
                .or(pid)
                .ok_or_else(|| anyhow!("instance '{}' was not started by the bridge: pass its `pid`", instance.id))?;
            let stem = match &instance.launch {
                Some(l) => l.stem.clone(),
                None => new_run_stem(&format!("{}-attached-{pid}", instance.id))?,
            };
            instance.debug = Some(debugger::attach(pid, &instance.id, &stem)?);
        }
        let session = instance.debug.as_ref().expect("attached above");
        let mut v = session.describe();
        if break_in {
            v["stopped"] = json!(session.break_in()?);
            v["next"] = json!(format!(
                "The app is stopped in the debugger: inspect it with instance_debug_command, then resume it \
                 with the command \"{}\" before driving the UI again.",
                session.resume_command()
            ));
        } else {
            v["stopped"] = json!(session.stop_status());
        }
        Ok(v)
    }

    /// Open a dump post-mortem in the platform debugger, as the pseudo-instance
    /// `id` (default: `dump-<file stem>`) for instance_debug_command; closed by
    /// [`Self::kill`].
    pub fn open_dump(&mut self, path: &Path, id: &str) -> Result<Value> {
        let id = if id.is_empty() {
            format!("dump-{}", sanitize(&path.file_stem().unwrap_or_default().to_string_lossy()))
        } else {
            sanitize(id)
        };
        if self.instances.contains_key(&id) || self.dumps.contains_key(&id) {
            bail!("instance '{id}' already exists (instance_stop it first or pass a distinct id)");
        }
        let session = debugger::open_dump(path, &new_run_stem(&id)?)?;
        let v = describe_dump(&id, &session);
        self.dumps.insert(id, session);
        Ok(v)
    }

    /// The instances stopped in their debugger, as "'<id>': <stop>" joined by
    /// "; " (empty when none is).
    pub fn debugger_stops(&self) -> String {
        let mut stops: Vec<String> = self
            .instances
            .values()
            .filter_map(|i| Some(format!("'{}': {}", i.id, i.debug.as_ref()?.stop_status()?)))
            .collect();
        stops.sort();
        stops.join("; ")
    }

    /// Run a command in an instance's debugger (or an open dump's) and return its
    /// output, plus the stop the process is in afterwards (null: running).
    pub fn debug_command(&mut self, id: &str, command: &str) -> Result<Value> {
        if let Some(session) = self.dumps.get(id) {
            let output = session.command(command)?;
            return Ok(json!({ "output": output, "stopped": session.stop_status() }));
        }
        let instance = self.lookup(id)?;
        let session = instance.debug.as_ref().ok_or_else(|| {
            anyhow!("no debugger is attached to instance '{}': instance_debug attaches one", instance.id)
        })?;
        let output = session.command(command)?;
        Ok(json!({ "output": output, "stopped": session.stop_status() }))
    }

    /// The dumps a live session or an open post-mortem still points at.
    fn protected_dumps(&self) -> HashSet<PathBuf> {
        let mut out: HashSet<PathBuf> = self
            .instances
            .values()
            .filter_map(|i| Some(i.debug.as_ref()?.dump_path().to_path_buf()))
            .collect();
        out.extend(self.dumps.values().map(|s| s.dump_path().to_path_buf()));
        out
    }

    /// Keep the log directory from filling up with crash dumps: a full minidump
    /// of Sourcetrail is hundreds of megabytes, and nothing else removes them.
    pub fn prune_dumps(&self) -> usize {
        let keep = dumps_kept();
        if keep == 0 {
            return 0;
        }
        prune_dumps_in(&home().join("logs"), keep, &self.protected_dumps())
    }
}

/// An open dump in instance_list.
fn describe_dump(id: &str, session: &DebugSession) -> Value {
    let mut v = json!({ "id": id, "running": false, "stopped": session.stop_status() });
    if let (Value::Object(m), Value::Object(dbg)) = (&mut v, session.describe()) {
        m.extend(dbg);
    }
    v
}

impl Default for InstanceManager {
    fn default() -> Self {
        Self::new()
    }
}

// Spawn-side readiness: retry until the app can actually be *driven*, failing fast
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
                launch.console_file().display()
            );
        }
        if let Ok(bridge) = Bridge::connect_instance(id) {
            let hs = bridge.handshake();
            if hs.get("ok").and_then(Value::as_bool) == Some(true) {
                return Ok(AppInstance { id: id.to_string(), child: Some(child), bridge, launch: Some(launch), debug: None });
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
                launch.console_file().display()
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

/// Where named profiles live: `<home>/profiles/<name>`. Outside any checkout on
/// purpose: a profile holds machine state (window geometry, recent projects).
pub fn profiles_root() -> PathBuf {
    home().join("profiles")
}

/// The --user-data-dir for a profile: `None` for the developer's own settings,
/// else the profile's directory, or a throwaway copy of it when `fresh`.
/// Returns the directory and whether it is a copy to delete afterwards.
fn user_data_dir_for(profile: &str, fresh: bool, id: &str) -> Result<(Option<PathBuf>, bool)> {
    user_data_dir_in(&profiles_root(), &std::env::temp_dir().join("sourcetrail-mcp"), profile, fresh, id)
}

/// [`user_data_dir_for`] with the profile root and the scratch directory for fresh
/// copies passed in (tested).
///
/// A profile's directory is not created here: Sourcetrail creates a user-data
/// directory that does not exist yet and fills it with its default settings, but
/// leaves an existing (even empty) one as it is.
fn user_data_dir_in(root: &Path, scratch: &Path, profile: &str, fresh: bool, id: &str) -> Result<(Option<PathBuf>, bool)> {
    let profile = profile.trim();
    if profile.eq_ignore_ascii_case(USER_PROFILE) {
        if fresh {
            bail!("`fresh` needs an isolated profile; the '{USER_PROFILE}' profile is the developer's own settings");
        }
        return Ok((None, false));
    }
    let name = sanitize(if profile.is_empty() { DEFAULT_PROFILE } else { profile });
    let dir = root.join(&name);
    std::fs::create_dir_all(root).with_context(|| format!("create profile root {}", root.display()))?;
    if !fresh {
        return Ok((Some(dir), false));
    }
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or_default();
    let copy = scratch.join(format!("{name}-{id}-{stamp}"));
    std::fs::create_dir_all(scratch).with_context(|| format!("create {}", scratch.display()))?;
    // A profile that never ran has nothing to copy: the copy starts as defaults.
    if dir.is_dir() {
        copy_dir(&dir, &copy).with_context(|| format!("copy profile {} to {}", dir.display(), copy.display()))?;
    }
    Ok((Some(copy), true))
}

fn copy_dir(from: &Path, to: &Path) -> Result<()> {
    std::fs::create_dir_all(to)?;
    for entry in std::fs::read_dir(from)? {
        let entry = entry?;
        let target = to.join(entry.file_name());
        if entry.file_type()?.is_dir() {
            copy_dir(&entry.path(), &target)?;
        } else {
            std::fs::copy(entry.path(), &target)?;
        }
    }
    Ok(())
}

/// The stem of a new run of instance `id`: `<home>/logs/<id>-<unix ms>`. The
/// run's files share it (`.console.txt`, and with a debugger `.dmp`, `.cdb.log`,
/// ...), so every run keeps its own and before/after a change stay comparable.
fn new_run_stem(id: &str) -> Result<PathBuf> {
    let dir = home().join("logs");
    std::fs::create_dir_all(&dir).with_context(|| format!("create log dir {}", dir.display()))?;
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or_default();
    Ok(dir.join(format!("{id}-{stamp}")))
}

/// `stem` with `suffix` appended (not an extension replaced: ids contain dots).
fn with_suffix(stem: &Path, suffix: &str) -> PathBuf {
    let mut s = stem.as_os_str().to_owned();
    s.push(suffix);
    PathBuf::from(s)
}

/// How many crash dumps to keep in the log directory, newest first. They are
/// large and nothing else removes them, so a debugging session's handful of
/// crashes would otherwise pile up. `SOURCETRAIL_MCP_KEEP_DUMPS` sets another
/// number; 0 keeps every one.
const DUMPS_KEPT: usize = 3;

fn dumps_kept() -> usize {
    std::env::var("SOURCETRAIL_MCP_KEEP_DUMPS").ok().and_then(|v| v.parse().ok()).unwrap_or(DUMPS_KEPT)
}

/// Delete all but the `keep` newest dumps in `dir`, never one in `protected`.
/// Returns how many went. Pure except for the removals (tested).
///
/// A protected dump counts towards `keep`: an instance holding three of them is
/// already at the limit, rather than three above it.
fn prune_dumps_in(dir: &Path, keep: usize, protected: &HashSet<PathBuf>) -> usize {
    let mut dumps: Vec<(std::time::SystemTime, PathBuf)> = std::fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .map(|entry| entry.path())
        .filter(|p| matches!(p.extension().and_then(|e| e.to_str()), Some("dmp") | Some("core")))
        .filter_map(|p| p.metadata().ok().and_then(|m| m.modified().ok()).map(|t| (t, p)))
        .collect();
    dumps.sort_by(|a, b| b.0.cmp(&a.0)); // newest first
    let mut kept = dumps.iter().filter(|(_, p)| protected.contains(p)).count();
    let mut removed = 0;
    for (_, path) in dumps.into_iter().filter(|(_, p)| !protected.contains(p)) {
        if kept < keep {
            kept += 1;
        } else if std::fs::remove_file(&path).is_ok() {
            removed += 1;
        }
    }
    removed
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

    fn scratch_root(tag: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("sourcetrail-mcp-test-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        dir
    }

    #[test]
    fn the_user_profile_means_no_user_data_dir() {
        let root = scratch_root("user");
        assert_eq!(user_data_dir_in(&root, &root, "user", false, "x").unwrap(), (None, false));
        assert!(user_data_dir_in(&root, &root, "User", true, "x").is_err());
    }

    #[test]
    fn a_named_profile_persists_and_is_left_for_the_app_to_create() {
        let root = scratch_root("named");
        let (dir, fresh) = user_data_dir_in(&root, &root, "", false, "x").unwrap();
        assert_eq!(dir, Some(root.join(DEFAULT_PROFILE)));
        assert!(!fresh);
        // Not created: the app fills a missing directory with its defaults.
        assert!(!root.join(DEFAULT_PROFILE).exists());
        let (dir, _) = user_data_dir_in(&root, &root, "my profile", false, "x").unwrap();
        assert_eq!(dir, Some(root.join("my-profile")));
    }

    #[test]
    fn pruning_keeps_the_newest_dumps_and_never_a_protected_one() {
        let dir = scratch_root("prune");
        std::fs::create_dir_all(&dir).unwrap();
        let base = std::time::SystemTime::now() - Duration::from_secs(1000);
        let mut paths = Vec::new();
        for i in 0..5u64 {
            let p = dir.join(format!("run-{i}.dmp"));
            let f = std::fs::File::create(&p).unwrap();
            f.set_modified(base + Duration::from_secs(i * 10)).unwrap();
            paths.push(p);
        }
        std::fs::write(dir.join("run-0.console.txt"), "kept").unwrap();
        // The oldest is protected: kept beyond the limit, and counted towards it.
        let protected: HashSet<PathBuf> = [paths[0].clone()].into_iter().collect();
        assert_eq!(prune_dumps_in(&dir, 3, &protected), 2);
        for (i, p) in paths.iter().enumerate() {
            assert_eq!(p.exists(), matches!(i, 0 | 3 | 4), "{}", p.display());
        }
        assert!(dir.join("run-0.console.txt").exists(), "only dumps are pruned");
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn fresh_copies_the_profile() {
        let root = scratch_root("fresh");
        let scratch = root.join("scratch");
        std::fs::create_dir_all(root.join("p").join("sub")).unwrap();
        std::fs::write(root.join("p").join("sub").join("settings.json"), "{}").unwrap();
        let (dir, fresh) = user_data_dir_in(&root, &scratch, "p", true, "i").unwrap();
        let dir = dir.unwrap();
        assert!(fresh);
        assert!(dir.starts_with(&scratch));
        assert_eq!(std::fs::read_to_string(dir.join("sub").join("settings.json")).unwrap(), "{}");
        // A profile that never ran: nothing to copy, the app starts from defaults.
        let (dir, _) = user_data_dir_in(&root, &scratch, "never-ran", true, "i").unwrap();
        assert!(!dir.unwrap().exists());
        let _ = std::fs::remove_dir_all(&root);
    }
}
