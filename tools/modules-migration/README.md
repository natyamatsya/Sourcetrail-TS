# C++20-modules migration tooling

Scripts that automate the mechanical parts of the classic-includer → importer migration
(context/DESIGN_INDEXER_MODULARIZATION.md). The compiler is the arbiter — these tools narrow the
candidate set and generate first drafts; every batch is still verified dual-mode (OFF + ON:
build, full suite, headless Usages index 529/2041/0).

## convertible.py — the batch planner (durable, run every batch)

Derives the module surface from the `.cppm` wrappers themselves (purview includes → owning
module) and classifies every classic `.cpp`:

- **CONVERTIBLE** — prints the headers to guard under `SRCTRL_MODULE_BUILD` and the exact
  `import` block to paste (imports go AFTER all textual includes — abi_tag rule).
- **BLOCKED** — the dirty classic headers with the reason (closure reaches a modularized header /
  unguarded fwd decl of an attached type / stdexec in closure). The ranked blocker list at the
  end is the shopping list for the next modularization slice.
- **SKIP** — definer TUs (classic seams of modularized components), direct stdexec includers,
  TUs with no modularized deps.

Caveats: purely textual (no preprocessing — `GATED_CLEAN` allowlists headers like tracing.h whose
modularized includes sit behind an off-by-default gate); only sees DIRECT purview includes
(`EXTRA_ATTACHED` covers transitively-attached family headers if a wrapper relies on that);
multi-wrapper macro headers need `OWNER_OVERRIDES` (logging.h belongs to srctrl.logging even
though every wrapper includes it).

### `--apply <tu.cpp>...` — mechanical conversion

Rewrites each CONVERTIBLE TU in place: wraps its modularized includes under
`#ifndef SRCTRL_MODULE_BUILD`, appends the import block after the last include, and handles the
logging.h macro-header case (`SRCTRL_LOGGING_VIA_IMPORT` + textual logging.h + import). Prints
the per-target CMake `CXX_SCAN_FOR_MODULES` entries to wire manually. Review the diff; build.

### `--index [db]` — self-index closure (the dogfooding loop)

Replaces the textual include scan with the `EDGE_INCLUDE` edges of Sourcetrail's own self-index
(default `Sourcetrail.srctrl.db`): preprocessed truth, so gated headers and exotic include
spellings resolve exactly. The DB reflects the build it indexed — refresh after structural
changes (modules-ON build, then `Sourcetrail index --full Sourcetrail.srctrl.toml`).
Forward-declaration dirtiness still comes from the textual scan.

## cpp2inl.py / hdr2mod.py — one-shot generators (drafts, not truth)

`cpp2inl.py` turns a component's `.cpp` into an all-`inline` `.inl` (drops includes — reattach
them classified: family-internal unguarded, cross-module/std under `#ifndef
SRCTRL_MODULE_PURVIEW`). `hdr2mod.py` sweeps a header (SrctrlModule.h + include guards +
`SRCTRL_EXPORT`).

Known regex gaps — ALWAYS review the diff and let the OFF build arbitrate:
- multi-line signatures, `= default` definitions, `static`/uppercase constant members may miss
  their `inline` (symptom: duplicate symbols at link);
- access specifiers can catch a stray `SRCTRL_EXPORT` (`SRCTRL_EXPORT public:`);
- `SRCTRL_EXPORT` must precede `template`, the sweep puts it on the class line;
- family-internal includes/fwd decls must NOT be guarded — the sweep guards everything.

## The playbook (what the tools encode, learned batches 1–6 + the Phase-6 pivot)

0. **THE ATTACHMENT PIVOT (Phase 6, supersedes the caveats in 6/9/11):** `SRCTRL_EXPORT` is
   `export extern "C++"`, and every wrapper wraps its whole textual purview in an
   `extern "C++" { }` block — ALL first-party entities (declarations AND .inl definitions)
   attach to the GLOBAL module ([module.unit]/7) with ordinary mangling, while imports still
   grant name visibility. Consequences: a textual parse and an import of the same entity MERGE
   instead of clashing (include-before-import direction only — rule 1 still orders that);
   fwd decls of module-owned types in classic headers are harmless; classic out-of-line
   definitions link for importer callers; moc-generated TUs coexist freely (Q_OBJECT headers
   stay textual forever); the only remaining conversion blocker is stdexec (rule 4). Bare
   `export` had given entities strong module ownership — the srctrl.messaging park, the
   toLowerCase lesson, and the include-only-contract asymmetry were all costs of that choice.
   `SRCTRL_MODULE_BUILD` is defined PER SOURCE FILE (CMake, next to CXX_SCAN_FOR_MODULES), so
   it means "this TU imports" — never define it target-wide (moc TUs must not see it).
1. Imports after ALL textual includes (abi_tag redeclaration).
2. Import what you USE — no re-exports between srctrl modules.
3. `LOG_*` users: TU-local `SRCTRL_LOGGING_VIA_IMPORT` before includes, `import srctrl.logging;`.
   NOT usable when the TU's remaining textual closure itself expands `LOG_*` (e.g. Qt headers
   pulling FilePath.inl) — the backend must exist at include time; keep logging.h fully classic
   there (post-pivot the textual backend and the BMI are the same entities).
4. stdexec in the closure ⇒ unconvertible (clang-scan-deps).
5. Dropped includes expose transitive std headers — add them explicitly.
6. (Largely retired by rule 0.) Under bare `export` the mangling law was: free functions in
   module content module-mangle (must be inline); members of attached classes stay
   ordinary-mangled for classic callers only. Post-pivot everything is ordinary-mangled; keep
   definitions inline in headers/.inls anyway (dual-build ODR merging relies on it).
7. Anything the wrapper itself references (vtables of concrete types!) must be inline in-module.
8. Family-internal includes/fwd decls unguarded; family fwd decls need `SRCTRL_EXPORT`.
9. Classic-with-cpp deps of a wrapper (ToolChain, utilityUuid) go in its GMF, never the purview —
   BUT a GMF include is only legal while its whole include closure is module-free. Purview guards
   (`#ifndef SRCTRL_MODULE_PURVIEW`) do NOT protect a GMF parse (the macro is defined only after
   `export module`), so a module-owned header reached textually from a GMF attaches its closure to
   the global module — ill-formed against an import of the owner, and clang diagnoses it lazily,
   far away, in whatever sibling partition first merges both BMIs (`repro-gmf-attachment/`). When
   a classic GMF dep later joins a module, every wrapper pre-loading it must switch to importing
   the owning module (MessageStatus/`srctrl.cxx:tooling` lesson).
10. Cycle-prone aggregates (a factory naming its whole family) live in a dedicated `.inl`
    included only by the wrapper (last) + a classic emission TU whose non-inline function
    odr-uses them (an unused address-take is dropped without emitting).
11. A family class whose implementation stays a classic TU forever (MessageQueue's stdexec
    pimpl) goes in the GLOBAL module via a purview-gated linkage-specification:
    `#ifdef SRCTRL_MODULE_PURVIEW / export extern "C++" { / #endif` around the class.
    [module.unit]/7 attaches linkage-spec contents to the global module, so members and
    static data keep ordinary mangling and the classic definitions link for importers.
    Without it, wrapper-instantiated inline callers reference `Member@module` symbols
    nothing defines (the mangling-law flip side of rule 6).
