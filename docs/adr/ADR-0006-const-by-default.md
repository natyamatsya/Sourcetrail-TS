# ADR-0006: `const` by default; `mutable` only for synchronization/caching

- **Status:** Accepted
- **Date:** 2026-07-19
- **Deciders:** natyamatsya
- **Related:** [ADR-0001](ADR-0001-expected-error-channel.md) (the concurrency model `const`-correctness
  supports).

## Context

`const`-correctness documents intent, prevents accidental mutation, and — since C++11 — carries a
**thread-safety** contract: as Herb Sutter argues, `const` now means *logically immutable and safe to
read concurrently without external synchronization*. The counterpart is that a member that must
change even on a `const` object (a mutex, an atomic, a memoization cache) should be `mutable` and
internally synchronized, so the enclosing member function can stay `const`-qualified naturally rather
than being forced non-`const` (or casting `const` away).

## Decision

1. **What can be `const`, should be `const`.** Default to `const` and drop it only where mutation is
   actually required:
   - local variables,
   - member functions that don't modify observable state,
   - by-reference parameters (`const T&` / `std::string_view`) and pointees (`const T*`),
   - data members that are never reassigned after construction.

2. **A `const` member function is a thread-safety promise** (Sutter): it must be safe to call
   concurrently on a shared object. If it can't be, it isn't really `const`.

3. **`mutable` is allowed — and preferred over dropping `const`/`const_cast` — for members that do
   not affect the object's observable value**, specifically:
   - **mutexes / locks** guarding the object,
   - **atomics**,
   - **caches / memoization** (lazily computed, logically-`const` results).

   Using `mutable` this way lets the natural read operations stay `const`-qualified. `mutable` must
   **not** be used to smuggle observable-state changes past `const`.

4. **Exception — do not `const`-qualify a return-by-value of a movable type** (`const T foo()`): it
   inhibits move semantics and buys nothing (per Sutter, *GotW #6a*). "`const` by default" applies to
   variables, parameters, pointees, and member functions — not to by-value return types.

## Consequences

**Positive** — intent is explicit; `const` doubles as a concurrency signal; read APIs stay
`const`-qualified without contortions thanks to `mutable` mutex/cache members.

**Negative** — discipline: contributors must add `const` proactively, and reviewers watch for
missing `const` and for `mutable` used beyond locks/atomics/caches.

**Neutral** — enforcement can later be assisted by clang-tidy (`misc-const-correctness`); this ADR is
the human rule until then.

## References
- Herb Sutter, *You Don't Know `const` and `mutable`* (C++ and Beyond) —
  <https://herbsutter.com/2013/01/01/video-you-dont-know-const-and-mutable/>,
  announcement: <https://isocpp.org/blog/2012/12/you-dont-know-const-and-mutable-herb-sutter>.
- Herb Sutter, *GotW #6a: Const-Correctness, Part 1* —
  <https://herbsutter.com/2013/05/24/gotw-6a-const-correctness-part-1-3/>.
