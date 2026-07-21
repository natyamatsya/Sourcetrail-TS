# ADR-0008: Headless indexing has a guaranteed terminal event, outcome exit codes, and a no-progress watchdog

- **Status:** Accepted
- **Date:** 2026-07-21
- **Deciders:** natyamatsya
- **Related:** ADR-0001/ADR-0004 (std::expected + named error enums),
  `src/lib_core/data/indexer/IndexingOutcome.h`, `src/lib_core/utility/scheduling/TaskFinally.{h,cpp}`,
  `Project::refresh`/`Project::buildIndex` boundaries, `QtCoreApplication` (exit codes + watchdog),
  commit `6ac4ca7c`.

## Context

In headless mode (`Sourcetrail index ...`) the only thing that ends the process was
`MessageIndexingFinished` → `MessageQuitApplication` — a message emitted by a stage near the end
of the indexing task tree. When any earlier stage died abnormally, nobody emitted it: the generic
`TaskRunner` caught the exception, logged one line, terminated the running tasks, and the Qt event
loop idled forever. Observed live during the Phase 5 self-index: a `nlohmann::json` type error in
the CMake File API reader killed the refresh task and the run hung indefinitely — and, once
killed, still **exited 0**. Silence was indistinguishable from success, for scripts, CI, and our
own verification loops.

The root defect is structural, not the specific exception: **the quit condition was a
happy-path event owned by an interior pipeline stage.** Any failure before that stage skips it.

## Decision

Three layers, all error propagation via `std::expected` with named scoped-enum codes
(`IndexingOutcome = std::expected<void, ExpectedError<IndexingErrorCode>>`); exceptions are
converted exactly once at the existing boundaries and never used for control flow.

1. **The terminal event is owned by the pipeline root, by construction.**
   `MessageIndexingFinished` carries an `IndexingOutcome`. `Project::refresh`/`buildIndex` are
   `utility::expectedFromExceptions` boundaries around the real bodies: an escaping throw becomes
   a failed outcome plus the terminal event (these entry points run inside a generic scheduled
   task whose runner would otherwise swallow the throw). The dispatched task tree is wrapped in
   `TaskFinally`, a scheduling decorator whose callback fires exactly once with the terminal
   cause — success (silent; the tree's final stage dispatches it), failure (stage failed or
   threw; the child `TaskRunner` converts throws to `STATE_FAILURE`), or termination. Interior
   stages no longer dispatch the terminal event on abnormal paths.

2. **Failure is observable: outcome → exit code.** `MessageQuitApplication` carries an exit code;
   headless maps the outcome onto it (0 success, 1 failure/terminated). A wedged, failed, or
   interrupted run can no longer report success.

3. **A no-progress watchdog bounds the unenumerable rest.** Layers 1–2 fix event-flow bugs but
   cannot see genuine deadlocks, a parent waiting on a silently dead worker pool, or IPC stalls.
   The headless app object stamps an atomic activity timestamp from the status/progress message
   handlers and checks it from a main-thread timer: no progress for
   `SOURCETRAIL_HEADLESS_WATCHDOG_MINUTES` (default 30, `0` disables) aborts with exit code 3.
   It watches *activity*, not completions, so one legitimately slow TU does not false-trigger.

## Consequences

- Every headless run terminates with a truthful exit code: 0 success, 1 failed/terminated
  pipeline, 3 watchdog abort. CI and scripts must treat non-zero as failure (and can).
- New pipeline stages need no discipline to keep the guarantee — the root decorator owns the
  terminal event. A stage that dispatches `MessageIndexingFinished` itself on an abnormal path
  would double the event; don't (see `TaskFinishParsing::terminate`).
- The watchdog default (30 min of *zero* messages) is deliberately generous; tune per
  environment via the env var rather than lowering the default.
- `TaskFinally` is generic scheduling infrastructure; unit-tested for
  success/failure/throw/terminate and fire-once (`TaskSchedulerTestSuite`).
