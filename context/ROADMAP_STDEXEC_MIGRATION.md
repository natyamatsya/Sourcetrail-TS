# stdexec / senders-receivers migration — roadmap & scope

Where the stdexec migration stands, what "refactor the messages to stdexec"
concretely means, and how it orders against the agent-UI control work
([`DESIGN_AGENT_UI_CONTROL.md`](DESIGN_AGENT_UI_CONTROL.md)).

Descriptive, not a plan of record — regenerate the "done" list from `jj log` if
the branch moves.

## Guiding principle (already established)

- **Value channel carries domain errors** as `std::expected`; the sender error
  channel (`set_error`) is reserved for the truly exceptional; cancellation is
  `set_stopped`. This is [ADR-0001](../docs/adr/ADR-0001-expected-error-channel.md),
  implemented by the `ExpectedPipeline` combinators
  (`ThenOk`/`AndThenOk`/`OrElseOk`, `src/lib/utility/execution/ExpectedPipeline.h`).
- **The message front door stays.** `Message<T>::dispatch()` and
  `MessageListener<T>` (`src/lib/utility/messaging/`) are deliberately *not*
  rewritten into sender pipelines. Messages remain the high-level "what happened"
  actor; only the **execution underneath** moves onto stdexec. `Message.h` /
  `MessageListener.h` are untouched by all the work below.

So "refactor the messages to stdexec" does **not** mean replacing the observer
model — it means retiring the old `Task`/`TaskScheduler` machinery that currently
*runs* message handlers, in favor of the `execution::ISchedulers` foundation.

## Done (foundation + early phases)

| Commit | What |
|---|---|
| `05b005e4` | **Foundation** — `execution::ISchedulers` (`io()`/`compute()`/`ui()`), type-erased `AnySenderOf`/`AnyScheduler`, `StdexecPrelude`, the Qt bridge (`QtThreadScheduler` posting via `QMetaObject::invokeMethod`, completing `set_stopped` if the anchor dies), and the `ExpectedPipeline` error combinators |
| `0d6028b9` | `onUi(anchor, work)` GUI-hop helper; migrate `QtStatusBarView` off `QtThreadedFunctor` |
| `00dee512` | **MessageQueue loop event-driven** via `stdexec::run_loop` (a coalesced drain scheduled by `pushMessage`, replacing the 25 ms poll) — dispatch semantics unchanged |
| `79f046c6` | **Application owns an injectable `execution::ISchedulers*`**, wired at the composition root (`main.cpp`) — scaffolding, not yet consumed |
| `94e1f44a` | `TaskBuildIndex` structured cancellation — `stdexec::inplace_stop_source` + `inplace_stop_callback`, `std::jthread` auto-join |
| `d637c411` | fresh run_loop per session; smoke harness (`scripts/smoke.sh`) + run_loop regression tests |

## The seam today

A queued message is delivered like this (`src/lib/utility/messaging/MessageQueue.cpp`):

```
Message<T>::dispatch() → pushMessage() → [run_loop drain]
  → sendMessage()        : iterate MessageListener<T>, call handleMessage()   (inline)
  or sendMessageAsTask() : wrap each listener's handleMessageBase() in a
                           TaskLambda → TaskGroupParallel/Sequence
                           → Task::dispatch(schedulerId, taskGroup)   ← OLD path
```

That last hop lands on `TaskScheduler`/`TaskManager` — a **polling** loop with
`TaskGroupParallel` spawning a raw `std::thread` per task. The event-driven
run_loop (done) fixed the *queue*; the *handler execution* still rides the old
Task machinery. The injected `ISchedulers` exists but nothing consumes it yet.

## Next increment — "route message-handler execution onto `ISchedulers`"

The smallest, highest-value step, and the one the scaffolding was built for:

- In `sendMessageAsTask`, run each per-listener unit of work on
  `Application::getSchedulers()->io()` (or `compute()`) instead of wrapping it in
  a `TaskLambda` on the old `TaskScheduler`. A listener that must touch Qt hops via
  `ui()` / `onUi`.
- Preserve everything the front door guarantees: `TabId` scheduler routing, the
  parallel-vs-sequence choice, message filters, `dispatchImmediately`, and the
  inline `sendMessage()` path.
- Retire `TaskScheduler`/`TaskGroupParallel` **for message delivery** (they remain
  for the indexer until Phase 3).
- Extend `scripts/smoke.sh` + `MessageQueueTestSuite` to cover the new routing.

This is a bounded change to one function's back half, on top of finished
scaffolding — not a rewrite of the 91 message types or their listeners.

## Later — subsystem migration (Phase 3+)

Incrementally replace the remaining old-async users with sender pipelines, then
retire `Task`/`TaskScheduler`/`TaskGroup*` entirely:

- **Indexer**: `TaskBuildIndex` (cancellation already stdexec), `TaskMergeStorages`,
  `TaskInjectStorage`, `TaskFillIndexerCommandQueue` — `TaskGroupParallel` → pool
  schedules.
- **Storage**: bulk writes already use `exec::static_thread_pool`
  (`ConcurrentTursoWriter`); fold the surrounding `Task` wrappers into senders.
- **GUI**: header-search `std::thread`s (`QtProjectWizardContent*`) → `io()` +
  `onUi` for results.

~54 `std::thread` sites and ~20 `TaskGroupParallel` call sites are the long tail
here; this is the bulk of the work and can proceed subsystem-by-subsystem.

## Explicitly out of scope

Rewriting `Message<T>` / `MessageListener<T>` into senders/receivers. The observer
front door is the stable public surface the rest of the app (and the agent-UI
seam) depends on. Keeping it is what makes the migration incremental.

## Sequencing vs. the agent-UI work

**The agent-UI control work does not need to wait for this migration, and this
migration does not need to wait for it.** They are orthogonal:

- The agent-UI seam ([`DESIGN_AGENT_UI_CONTROL.md`](DESIGN_AGENT_UI_CONTROL.md))
  rides the **preserved front door** — `Message<T>::dispatch()`,
  `MessageListener<T>`, `StorageAccess`. This migration changes only the execution
  *beneath* that front door, which the agent-UI code never touches.
- Agent-UI **Phase A** (offscreen GUI + `QMainWindow::grab()`) is independent of
  the message system entirely.
- Synergy runs the *other* way: the agent-UI harness (replay a message script
  offscreen, diff `get_ui_state`) would be an excellent **behavioural regression
  net for this migration** — a way to prove that moving handler execution onto
  schedulers changes nothing observable.

**Recommendation:** do them in parallel. If forced to pick a first step, do
agent-UI **Phase A** (cheap, independent, immediately lets the agent
screenshot-verify UI changes) and the stdexec **"route handlers onto ISchedulers"**
increment (well-scoped, on finished scaffolding) — neither blocks the other.
Reserve the big subsystem migration (Phase 3+) for after the agent-UI harness
exists, so it can guard the change.

## Verification assets

- `scripts/smoke.sh` — unit (concurrency suites) + headless `--full` index of the
  tutorial fixture + SIGINT-mid-index cancellation check.
- `src/test/MessageQueueTestSuite.cpp` — run_loop regression tests (pre-start drain;
  concurrent-burst delivery under coalesced wakeups).
