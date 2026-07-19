# ADR-0005: No `using namespace` at file scope; `using namespace std` is banned outright

- **Status:** Accepted
- **Date:** 2026-07-19
- **Deciders:** natyamatsya

## Context

A file-scope `using namespace X;` (a *using-directive*) dumps an entire namespace into the
translation unit. That causes ambiguities and ADL surprises, makes overload resolution depend on
which headers happen to be included, and — most importantly — hides where a name actually comes
from: `unique_ptr<CompilationDatabase>` gives no hint that `unique_ptr` is `std::` and
`CompilationDatabase` is `clang::tooling::`, and only compiles because some `using namespace std;` /
`using namespace clang::tooling;` sits at the top of the file. `std` is the worst offender: it is
huge, grows every standard, and collides with common identifiers.

## Decision

1. **`using namespace std;` is forbidden everywhere** — at file scope *and* inside a function/block.
   Always write the `std::` qualification explicitly.

2. **No using-directive (`using namespace X;`) at file / namespace / global scope, for any
   namespace.** Headers must never contain a using-directive at all (it leaks into every includer).

3. **Other namespaces may be imported only when well-scoped.** Inside a function or block, a
   `using namespace some::domain;` or — preferred — narrow using-*declarations* (`using clang::tooling::CompileCommand;`)
   are acceptable to cut local noise. At file scope, qualify explicitly (or use a short namespace
   alias, `namespace ct = clang::tooling;`).

4. Applies to new and modified code; existing violations (~22 `.cpp` files currently carry
   `using namespace std;`) are migrated opportunistically as files are touched, not in a big-bang
   sweep.

## Consequences

**Positive** — every name's origin is visible at the use site; no whole-namespace leakage, no
include-order-dependent overload resolution, no `std`-vs-local collisions.

**Negative** — more verbose (`std::` on every standard name); a touched file may need a one-time
requalification pass.

**Neutral** — narrow, locally-scoped `using X::Y;` declarations remain fine; this ADR is about
whole-namespace directives, especially at file scope.

## References
- `src/lib_cxx/project/utilitySourceGroupCxx.cpp` — first cleaned up (dropped file-scope
  `using namespace std;` + `using namespace clang::tooling;`).
