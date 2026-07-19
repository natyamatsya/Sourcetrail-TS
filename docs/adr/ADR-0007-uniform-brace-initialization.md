# ADR-0007: Prefer uniform (brace) initialization, except the `initializer_list` footguns

- **Status:** Accepted
- **Date:** 2026-07-19
- **Deciders:** natyamatsya

## Context

Brace initialization (`T x{...}`) is the safer default: it forbids narrowing conversions, avoids the
most-vexing-parse (`T x();` declaring a function), and value-initializes (`int n{}` is `0`, not
garbage). But `{}` is *not* universally safe — for a type with a `std::initializer_list` constructor,
braces prefer that overload, which is occasionally not the constructor you meant. Two cases bite in
this codebase:

- **Qt JSON containers.** `QJsonArray{existing}` invokes `QJsonArray(std::initializer_list<QJsonValue>)`
  instead of the copy constructor, silently wrapping `existing` inside a new one-element array. The
  same hazard applies to building `QJsonObject`/`QJsonValue` with braces. (The vendored
  `submodules/qt-json-query` documents this in its `BraceSafe.hpp` and its own ADR-001.)
- **Sized standard containers.** `std::vector<int>(3)` makes three elements; `std::vector<int>{3}`
  makes one element `3`. Same for `std::string(5, 'x')` vs. a brace list.

## Decision

1. **Default to brace initialization** — `T x{...}`, `T x{}`, member initializers, and return
   `{...}` — for the narrowing/init-safety and most-vexing-parse benefits.

2. **Use parentheses `()` (or explicit member calls) where an `initializer_list` overload would
   hijack the braces**, specifically:
   - **Qt JSON**: never brace-construct/copy `QJsonArray` / `QJsonObject` / `QJsonValue`. Copy with
     `()`, or build incrementally (`QJsonArray a; a.append(v);` / `QJsonObject o; o["k"] = v;`). When
     a factory returns one for `auto` call sites, return `json_query::BraceSafe<QJsonArray>` so
     `const auto x{factory()}` is safe (see `qt-json-query/include/json-query/utils/BraceSafe.hpp`).
   - **Count/size or allocator constructors** of standard containers: use `()`
     (`std::vector<int>(n)`, `std::string(n, c)`).

3. **Rule of thumb:** braces by default; drop to `()` only when the type has an `initializer_list`
   constructor *and* you want a different one.

## Consequences

**Positive** — narrowing bugs and most-vexing-parse disappear; initialization is explicit and
consistent.

**Negative** — contributors must recognize the `initializer_list`-hijack types (Qt JSON above all)
and switch to `()` there; a silent `QJsonArray` nesting bug is easy to introduce otherwise.

**Neutral** — `BraceSafe<T>` remains the escape hatch for returning Qt JSON containers to
brace-initializing `auto` call sites.

## References
- `submodules/qt-json-query/include/json-query/utils/BraceSafe.hpp` — the `QJsonArray{arr}` nesting
  footgun and the `BraceSafe<T>` wrapper (qt-json-query ADR-001).
- C++ Core Guidelines **ES.23**: prefer the `{}`-initializer syntax.
