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
| `945bd5316` | **Asking holds no thread** — `QuestionStateMachine::asked` / `DialogView::asked`, a sender of the answer; `confirm()` waits on it |
| `86414fad4` | **App strand** — `execution::Strand` (serial over a pool, a stdexec scheduler); the app `TaskScheduler` runs its loop there (`startSchedulerLoopOn`): event-driven passes instead of a 25 ms poll, held tasks interleave, idle edge kept. I/O pool: four threads |
| `feat(project)`, after `86414fad4` | **The project load as a pipeline** — strand → `io()` (open the index) → the question as a sender → strand (install), on behalf of the request |

## The seam today

A queued message is delivered like this (`src/lib/utility/messaging/MessageQueue.cpp`):

```
Message<T>::dispatch() → pushMessage() → [run_loop drain]
  → sendMessage()        : iterate MessageListener<T>, call handleMessage()   (inline)
  or sendMessageAsTask() : the message's listeners as one item of its queue
                           → execution::queueOf(schedulerId)->push(...)
```

Every scheduler id (the app, the background, each tab) has a `SerialQueue`
(`utility/execution/SerialQueue.h`, `TabQueues.h`) on a strand of its own over
`ISchedulers::io()`: items run one at a time in the order queued, and an item may
finish after it returns. A message's listeners are one item, in a row; a parallel
message's (an activation's graph and code) run side by side on
`ISchedulers::compute()`, and the next item starts once the last of them ended, so
the item holds the queue but no thread. Work a handler has run later on a queue is an
item too (`execution::post`, `postAfter` for the tooltips' delay on a timer), and the
queue is a stdexec scheduler: the load's pipeline continues on the app's queue with
`continues_on(appScheduler())`. Every item counts as its cause's work (`CauseWork`)
until it finished or `stop()` dropped it. Headless (no schedulers) a queue runs on a
small pool of its own.

## Next increment — "route message-handler execution onto `ISchedulers`"

*Done:* a message's listeners are one item of its tab id's serial queue, a parallel
message's run as senders on `compute()` (no thread per listener); the work handlers
posted to a scheduler as a `TaskLambda` is posted to the queue; `Settled` comes from
the work's completion (`CauseWork`). `TaskScheduler` is left with the indexer's task
trees, on the app's strand beside the app's queue, where `TaskLambda` remains a step
of a tree. Left for Phase 3: those trees (`TaskGroupParallel` still starts a thread per
child there) and the remaining `std::thread` sites.

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
