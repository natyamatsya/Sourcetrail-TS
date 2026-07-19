# ADR-0004: Fallible functions return `std::expected` with named scoped-enum error types

- **Status:** Accepted
- **Date:** 2026-07-19
- **Deciders:** natyamatsya
- **Related:** [ADR-0001](ADR-0001-expected-error-channel.md) (domain errors travel as `std::expected`
  in the value channel). This ADR refines *how the `E` in `std::expected<T, E>` is defined*. Exemplar:
  the vendored `submodules/qt-json-query` (`include/json-query/utils/JSONError.hpp`).

## Context

ADR-0001 established that expected/domain failures flow as `std::expected<T, E>` rather than through
exceptions, and noted "each domain needs a deliberate `E` (error enum/type)" — but left the shape of
`E` unspecified. In practice `E` was sometimes a bare `int` (a status/exit code), a `bool`, or a
sentinel value. Those are exception-free but weakly typed: the caller can't tell what the failure
*was* without out-of-band knowledge, the codes aren't reusable, and there's no single place that
documents a domain's failure modes.

`qt-json-query` already solves this cleanly and is the fork's reference for modern error handling: a
scoped enum per error domain, a `constexpr` message mapper, and `std::expected` / `std::unexpected`
throughout.

## Decision

**A fallible function returns `std::expected<T, E>` where `E` is a named, scoped enum — never a bare
`int`/`bool`/sentinel.** The convention (mirroring `qt-json-query`):

1. **Scoped enum, fixed underlying type**, one per error domain, listing the failure modes as
   reusable named constants:
   ```cpp
   enum class IndexerPrebuildError : std::uint8_t { IndexerExecutableMissing, SubprocessFailed };
   ```
2. **A `constexpr std::string_view to_std_sv(E) noexcept` message mapper**, `using enum` + a `switch`
   with no `default` (so a new constant is a compile warning until it's given a message):
   ```cpp
   constexpr std::string_view to_std_sv(IndexerPrebuildError e) noexcept {
       using enum IndexerPrebuildError;
       switch (e) {
       case IndexerExecutableMissing: return "sourcetrail_indexer executable is missing";
       case SubprocessFailed:         return "indexer prebuild subprocess exited non-zero";
       }
       return "unknown error";
   }
   ```
3. **Return `std::expected<T, E>`; fail with `return std::unexpected(E::Foo);`.** For
   nothing-to-return operations use `std::expected<void, E>` and `return {};` on success.
4. **Group codes by domain**; when a subsystem accumulates many enums, unify them into a single
   `Error{domain, code, detail}` value as `qt-json-query` does (`ErrorDomain` + per-domain enums).
5. `void`-returning status functions and out-params are replaced by `std::expected<void, E>` /
   `std::expected<T, E>` as they are touched.

First applied: `utility::runIndexerPrebuildMode` →
`std::expected<void, IndexerPrebuildError>` (was a bare exit-code `int`).

## Consequences

**Positive**
- Failures are self-describing and type-checked; callers `switch` on named constants or surface
  `to_std_sv(err)`.
- One place per domain enumerates + documents every failure mode; codes are reusable across call
  sites.
- Uniform with ADR-0001, `qt-json-query`, and the Rust indexer's `Result` idiom.

**Negative / costs**
- More ceremony than returning an `int`: a new domain needs an enum + a `to_std_sv` arm.
- The message mapper must be kept in sync (mitigated by the `default`-less `switch` warning).

**Neutral**
- Not retroactive-by-mandate: existing bare-status returns are migrated opportunistically as code is
  revisited, not in a big-bang sweep.

## References
- [ADR-0001](ADR-0001-expected-error-channel.md) — the value-channel error policy this refines.
- `submodules/qt-json-query/include/json-query/utils/JSONError.hpp` — the exemplar.
- `src/lib_cxx/project/utilitySourceGroupCxx.h` — `IndexerPrebuildError` + `runIndexerPrebuildMode`.
