# ADR-0001: Domain errors travel as `std::expected` in the sender value channel

- **Status:** Accepted
- **Date:** 2026-07-05
- **Deciders:** natyamatsya
- **Related:** senders/receivers migration ([messaging plan]), `src/lib/utility/execution/`,
  the fork's existing `utilityExpected` + `std::expected`-returning `Indexer::index()` /
  `InterprocessIndexer::work()`.

## Context

We are moving concurrency to a **senders/receivers** model (NVIDIA `stdexec`, vendored via vcpkg;
the Qt bridge lives in `src/lib_gui/qt/execution/`). A stdexec sender signals completion on one of
**three channels**:

| channel | meaning |
|---|---|
| `set_value(T...)` | success — carries the result |
| `set_error(E)` | failure — in practice `E = std::exception_ptr` |
| `set_stopped()` | cancellation |

If a `then`/transform **throws**, stdexec routes it to `set_error(std::exception_ptr)`. Using that as
the *primary* error path for ordinary, expected failures (file not found, parse error, IPC timeout)
is poor for async code:

- **Type-erased.** `std::exception_ptr` loses the static error type; every downstream receiver must
  `rethrow_exception` + `catch` to learn what happened.
- **Costly & awkward across async boundaries.** Exception propagation through queued/scheduled
  continuations is expensive and hard to reason about; it does not compose with `then`-chains.
- **All-or-nothing.** Once on the error channel, the value pipeline is abandoned; recovering
  (fallback, retry, map-error-at-a-boundary) means catching and re-entering the value path.

The codebase is already trending toward value-based errors: the fork added `utilityExpected` and made
`Indexer::index()` and `InterprocessIndexer::work()` return `std::expected`. The Rust indexer uses
`Result` idiomatically. This ADR makes that the explicit rule for the sender-based concurrency layer.

## Decision

**Domain/expected errors flow through the *value* channel as `std::expected<T, E>`. The `set_error`
(exception) channel is reserved for the *truly exceptional*. Cancellation uses `set_stopped`.**

1. **Value channel carries `std::expected<T, E>`.** A sender pipeline stays on `set_value` even when a
   domain operation fails; the failure is *data* (`std::unexpected(e)`), statically typed in `E`.

2. **`set_error` is reserved for the unrecoverable** — programmer errors, broken invariants, OOM,
   `std::bad_alloc` — conditions that *should* unwind and abort the operation, not be handled inline.

3. **Compose with the `ExpectedPipeline` combinators** (`src/lib/utility/execution/ExpectedPipeline.h`,
   `namespace execution`), each built on `stdexec::then` over `std::expected`:
   - `ThenOk(f)` — map the success value; errors forward unchanged.
   - `AndThenOk(f)` — monadic bind; `f` returns `std::expected` (short-circuits like Rust `?`).
   - `OrElseOk(f)` — recover from an error.
   - `TransformErrorOk(f)` — map `E` at a service boundary.

4. **Convert exceptions to `expected` at boundaries.** Third-party APIs that throw (Qt, LLVM/Clang,
   STL) still complete on `set_error`. Where such a failure is actually a *recoverable domain* error,
   wrap the call at the boundary (`try`/`catch` → `std::unexpected(...)`, or `stdexec::upon_error`)
   so it re-enters the value channel as `std::expected`. Genuinely fatal exceptions are left to
   propagate on `set_error`.

5. **Use `stdext::expected` for reference results.** When a step must yield a reference
   (`expected<T&, E>`), use the vendored `stdext::expected` extension
   (`src/external/stdcompat/stdext/`) rather than reworking the pipeline around a value copy.

## Consequences

**Positive**
- Errors are **statically typed, explicit, and cheap** — no `exception_ptr`, no async unwinding.
- Pipelines **compose** monadically; recovery/mapping are first-class (`OrElseOk`/`TransformErrorOk`).
- Uniform with the existing `utilityExpected` C++ code and the Rust indexer's `Result` style — one
  error model across the process boundary.
- Async continuations stay on the value path, which is far simpler to schedule and test.

**Negative / costs**
- More verbose than `throw`; each domain needs a deliberate `E` (error enum/type).
- Requires **discipline**: do not `throw` for expected failures; return `std::unexpected`.
- Boundary adapters are needed wherever we call exception-throwing libraries and want the failure
  handled as a domain error.

**Neutral**
- Exceptions are *not banned* — they remain the mechanism for the exceptional, and third-party throws
  still land on `set_error`. The rule is about **which channel carries which class of failure**.

## References
- `src/lib/utility/execution/ExpectedPipeline.h` — the combinators (`namespace execution`).
- `src/lib/utility/execution/StdexecPrelude.h`, `SenderAliases.h` — stdexec support.
- `src/lib_gui/qt/execution/` — Qt↔sender bridge (`QtThreadScheduler`, `SignalSender`, `QFuture`).

[messaging plan]: (see project memory: messaging → senders & receivers, conservative-now)
