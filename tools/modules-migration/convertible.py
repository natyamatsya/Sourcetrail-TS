#!/usr/bin/env python3
"""Which classic TUs can become module importers? (C++20-modules migration analyzer)

A TU is CONVERTIBLE when every one of its first-party includes is either
  (a) a modularized header  -> dropped behind `#ifndef SRCTRL_MODULE_BUILD`, replaced by an
      import of the owning module, or
  (b) a classic header whose transitive include closure contains NO modularized header and NO
      unguarded forward declaration of a module-attached type (both would put global-module and
      module-attached declarations of the same entity into one TU -- the type-boundary failure).

Extra rules learned batch by batch (docs/adr + context/DESIGN_INDEXER_MODULARIZATION.md):
  - stdexec anywhere in the TU's closure => NOT convertible: an importer TU must be scanned and
    clang-scan-deps chokes on stdexec's preprocessor.
  - A .cpp whose stem matches a modularized header is a DEFINER (classic seam of that component,
    e.g. NameHierarchy.cpp / ProjectSettingsFactory.cpp), not a consumer -- skipped.
  - Headers in GATED_CLEAN hide their modularized includes behind an off-by-default preprocessor
    gate this textual scan cannot evaluate (tracing.h).

The module surface is derived from the wrappers themselves: every `#include "X.h"` after
`#define SRCTRL_MODULE_PURVIEW` in a .cppm, mapped to that wrapper's `export module NAME;`
(partition names collapse to the primary module). Headers only attached TRANSITIVELY (a purview
header including a family sibling unguarded) are not seen -- if a family does that, list the
extra headers in EXTRA_ATTACHED below.

Usage:
  tools/modules-migration/convertible.py [paths-or-globs of .cpp files; default: src/**]
  tools/modules-migration/convertible.py --apply <tu.cpp> [...]
  tools/modules-migration/convertible.py --index [Sourcetrail.srctrl.db] [tus...]

--index replaces the textual include scan with the include edges of Sourcetrail's own
self-index (EDGE_INCLUDE = 256): preprocessed truth, so preprocessor-gated headers (tracing.h)
and exotic include spellings resolve exactly -- the dogfooding loop. The DB reflects the build
it indexed: refresh with a modules-ON build + `Sourcetrail index --full Sourcetrail.srctrl.toml`
after structural changes. Forward-declaration dirtiness still comes from the regex scan.

Output per TU: CONVERTIBLE with the exact import block to paste, BLOCKED with the dirty
headers (ranked blocker list at the end), or SKIP reasons (definer/stdexec/no modular deps).

--apply rewrites each CONVERTIBLE TU in place (guards the modularized includes under
SRCTRL_MODULE_BUILD, appends the import block after the last include, handles the logging.h
macro-header case via SRCTRL_LOGGING_VIA_IMPORT) and prints the CMake scan-list entries still
needed (per-target set_source_files_properties -- wired manually, targets differ). Review the
diff; the OFF build stays the arbiter.
"""

import os
import re
import subprocess
import sys
from collections import Counter
from functools import lru_cache

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True,
                      text=True).stdout.strip() or "."
SRC = os.path.join(ROOT, "src")

GATED_CLEAN = {"tracing.h"}
STDEXEC_MARKERS = {"StdexecPrelude.h", "execution.hpp", "any_sender_of.hpp"}
# Headers attached to a module only transitively (unguarded family includes inside a purview
# header). Map basename -> module.
EXTRA_ATTACHED: dict[str, str] = {}

# Headers included in MANY wrappers' purviews where only one module owns the entities. logging.h
# is the macro header: every wrapper includes it for the LOG_* macros, but the backend belongs to
# srctrl.logging -- without this, "last wrapper walked wins" hands out wrong imports.
OWNER_OVERRIDES = {"logging.h": "srctrl.logging"}

# ---------------------------------------------------------------- module surface

def module_surface():
    """basename -> owning module, from the wrappers' purview includes."""
    surface = dict(EXTRA_ATTACHED)
    for dirpath, _, files in os.walk(SRC):
        if "cxx_modules_poc" in dirpath:
            continue
        for f in files:
            if not f.endswith(".cppm"):
                continue
            text = open(os.path.join(dirpath, f), encoding="utf-8", errors="replace").read()
            m = re.search(r'^export module ([\w.]+)(?::[\w.]+)?;', text, re.M)
            if not m:
                continue
            module = m.group(1)
            # Split on the #define line, not the bare string -- it also appears in comments
            # (srctrl_logging.cppm line 8 taught us that).
            purview = re.split(r'^#define SRCTRL_MODULE_PURVIEW\b.*$', text,
                               maxsplit=1, flags=re.M)
            if len(purview) < 2:
                continue
            for inc in re.finditer(r'^#include "([^"]+)"', purview[1], re.M):
                base = os.path.basename(inc.group(1))
                if base not in OWNER_OVERRIDES:
                    surface[base] = module
    surface.update(OWNER_OVERRIDES)
    return surface

MOD = module_surface()

# ---------------------------------------------------------------- file index

by_base: dict[str, list[str]] = {}
for dirpath, _, files in os.walk(SRC):
    if "cxx_modules_poc" in dirpath:
        continue
    for f in files:
        if f.endswith((".h", ".hpp", ".inl")):
            by_base.setdefault(f, []).append(os.path.join(dirpath, f))

inc_re = re.compile(r'^\s*#include\s+["<]([^">]+)[">]', re.M)
fwd_re = re.compile(r'^\s*(?:class|struct)\s+([A-Za-z_]\w*)\s*;', re.M)


def read(path):
    try:
        return open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def direct_includes(path):
    return [os.path.basename(m) for m in inc_re.findall(read(path))]


def strip_module_guards(text):
    """Drop `#ifndef SRCTRL_MODULE_BUILD ... #endif` regions (per-TU importer guards in headers):
    for closure analysis a swept header counts as clean -- importing TUs never parse the guarded
    lines, and non-importing TUs never import, so the guarded content cannot clash."""
    out, depth = [], 0
    for line in text.splitlines(keepends=True):
        s = line.strip()
        if depth:
            if s.startswith("#if"):
                depth += 1
            elif s.startswith("#endif"):
                depth -= 1
            continue
        if s.startswith("#ifndef") and "SRCTRL_MODULE_BUILD" in s:
            depth = 1
            continue
        out.append(line)
    return "".join(out)


def header_includes(path):
    return [os.path.basename(m) for m in inc_re.findall(strip_module_guards(read(path)))]


def dirty_fwd_decls(path):
    return [n for n in fwd_re.findall(strip_module_guards(read(path))) if f"{n}.h" in MOD]


def module_graph():
    """primary module name -> set of directly imported primary modules (from the wrappers)."""
    graph = {}
    for dirpath, _, files in os.walk(SRC):
        if "cxx_modules_poc" in dirpath:
            continue
        for f in files:
            if not f.endswith(".cppm"):
                continue
            text = read(os.path.join(dirpath, f))
            m = re.search(r'^export module ([\w.]+?)(?::[\w.]+)?;', text, re.M)
            if not m:
                continue
            deps = {i for i in re.findall(r'^(?:export )?import ([\w.]+);', text, re.M)
                    if i != "std"}
            graph.setdefault(m.group(1), set()).update(deps)
    return graph


MODULE_IMPORTS = module_graph()


def loaded_closure(mods):
    """All module BMIs a TU importing `mods` transitively loads."""
    seen, stack = set(), list(mods)
    while stack:
        m = stack.pop()
        if m in seen:
            continue
        seen.add(m)
        stack.extend(MODULE_IMPORTS.get(m, ()))
    return seen


@lru_cache(maxsize=None)
def closure_dirt(base):
    """ALL dirty items in the textual closure, as (reason, owning module | None) pairs.
    owner=None means unconditionally blocking (stdexec). A modularized-header/fwd-decl item
    only blocks a TU whose imports transitively LOAD the owning BMI ([basic.link] clash);
    otherwise the closure parses as self-contained global-module text -- the accepted
    include-only two-entity mode (Q_OBJECT/moc surfaces live there permanently)."""
    if base in GATED_CLEAN:
        return ()
    if base in MOD:
        return ((f"includes modularized {base}", MOD[base]),)
    dirt, seen, stack = [], set(), [base]
    while stack:
        b = stack.pop()
        if b in seen:
            continue
        seen.add(b)
        if b in STDEXEC_MARKERS:
            return (("stdexec in closure (scanner-unconvertible)", None),)
        for p in by_base.get(b, []):
            for n in dirty_fwd_decls(p):
                dirt.append((f"{b}: fwd decl of attached type {n}", MOD[f"{n}.h"]))
            for i in header_includes(p):
                if i in GATED_CLEAN or i in seen:
                    continue
                if i in MOD:
                    dirt.append((f"{b} -> modularized {i}", MOD[i]))
                    continue
                if i in STDEXEC_MARKERS:
                    return (("stdexec in closure (scanner-unconvertible)", None),)
                stack.append(i)
    return tuple(dirt)


def analyze(tu):
    name = os.path.basename(tu)
    stem = name[:-4]
    if f"{stem}.h" in MOD or f"{stem}.hpp" in MOD:
        return ("SKIP", name, f"definer TU of modularized {stem} (classic seam)")
    if any(b.endswith(".inl") for b in direct_includes(tu)):
        return ("SKIP", name, "emission/seam TU (includes an .inl directly)")
    incs = [b for b in direct_includes(tu) if b != "Catch2.hpp"]
    if any(b in STDEXEC_MARKERS for b in incs):
        return ("SKIP", name, "includes stdexec directly (scanner-unconvertible)")
    modular = sorted(set(b for b in incs if b in MOD))
    if not modular:
        return ("SKIP", name, "no modularized includes")
    dirt_of = index_closure_dirt if INDEX_EDGES else closure_dirt
    imports = sorted(set(MOD[b] for b in modular))
    loaded = loaded_closure(imports)
    hard, soft = {}, {}
    for b in sorted(set(incs)):
        if b in MOD:
            continue
        # Post-pivot (SRCTRL_EXPORT = export extern "C++") every first-party entity is
        # global-module-attached, so a textual parse and an import of the same entity MERGE
        # ([module.unit]/7) -- modularized headers/fwd decls in the textual closure no longer
        # block (include-before-import direction only, which rule 1 guarantees). Only
        # stdexec (owner None: unscannable TU) still blocks.
        blocking = [r for r, o in dirt_of(b) if o is None]
        if blocking:
            hard[b] = "; ".join(dict.fromkeys(blocking))
        elif dirt_of(b):
            soft[b] = sorted({o for _, o in dirt_of(b)})
    if hard:
        return ("BLOCKED", name, hard)
    return ("CONVERTIBLE", name, (modular, imports, soft))


# ---------------------------------------------------------------- index-backed closure

INDEX_EDGES: dict[str, set] = {}   # file basename -> directly included file basenames


def load_index(db_path):
    """Include graph from the self-index: EDGE_INCLUDE (256) between NODE_FILE (262144) nodes."""
    import sqlite3
    con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    files = {}
    for nid, name in con.execute("SELECT id, serialized_name FROM node WHERE type = 262144"):
        # file serialization: '/<TAB>m<path><TAB>s...' -- take the path between \tm and \ts
        m = re.search(r'\tm([^\t]+)\ts', name)
        if m:
            files[nid] = os.path.basename(m.group(1))
    for src, dst in con.execute(
            "SELECT source_node_id, target_node_id FROM edge WHERE type = 256"):
        if src in files and dst in files:
            INDEX_EDGES.setdefault(files[src], set()).add(files[dst])
    con.close()


@lru_cache(maxsize=None)
def index_closure_dirt(base):
    """closure_dirt over the self-index include graph (preprocessed truth)."""
    if base in MOD:
        return ((f"includes modularized {base}", MOD[base]),)
    dirt, seen, stack = [], set(), [base]
    while stack:
        b = stack.pop()
        if b in seen:
            continue
        seen.add(b)
        for p in by_base.get(b, []):
            for n in dirty_fwd_decls(p):
                dirt.append((f"{b}: fwd decl of attached type {n}", MOD[f"{n}.h"]))
        for i in INDEX_EDGES.get(b, ()):
            if i in seen:
                continue
            if i in MOD:
                dirt.append((f"{b} -> modularized {i}", MOD[i]))
                continue
            if i in STDEXEC_MARKERS:
                return (("stdexec in closure (scanner-unconvertible)", None),)
            stack.append(i)
    return tuple(dirt)


IMPORT_COMMENT = ("// Imports come AFTER all textual #includes (include-before-import rule: textual libc++\n"
                  "// following BMI-merged declarations trips \"cannot add 'abi_tag' in a redeclaration\").\n")
LOGGING_COMMENT = ("// Module build: LOG_* macros stay textual (macros don't travel through imports); logging.h\n"
                   "// then yields macros only and the backend comes from `import srctrl.logging` below.\n")


def apply_conversion(tu, headers, imports):
    """Rewrite `tu` in place: guard modularized includes, append the import block."""
    lines = read(tu).split("\n")
    guard = set(headers)
    logging_via_import = "logging.h" in guard
    if logging_via_import:
        guard.discard("logging.h")  # macro header stays textual; backend via import

    out = []
    i = 0
    last_include_out_idx = -1
    while i < len(lines):
        l = lines[i]
        m = inc_re.match(l)
        if m and os.path.basename(m.group(1)) in guard:
            run = [l]
            while i + 1 < len(lines):
                nxt = inc_re.match(lines[i + 1])
                if nxt and os.path.basename(nxt.group(1)) in guard:
                    run.append(lines[i + 1])
                    i += 1
                else:
                    break
            out.append("#ifndef SRCTRL_MODULE_BUILD")
            out.extend(run)
            out.append("#endif")
            last_include_out_idx = len(out) - 1
        else:
            if m or l.startswith("#endif") or l.startswith("#ifndef SRCTRL_MODULE_BUILD"):
                last_include_out_idx = len(out)
            out.append(l)
        i += 1

    block = ["", IMPORT_COMMENT.rstrip("\n"), "#ifdef SRCTRL_MODULE_BUILD"]
    block += [f"import {m};" for m in imports]
    block += ["#endif"]
    insert_at = last_include_out_idx + 1 if last_include_out_idx >= 0 else 0
    out[insert_at:insert_at] = block

    if logging_via_import:
        out[0:0] = [LOGGING_COMMENT.rstrip("\n"), "#ifdef SRCTRL_MODULE_BUILD",
                    "#define SRCTRL_LOGGING_VIA_IMPORT", "#endif", ""]

    open(tu, "w").write("\n".join(out))


def audit_gmf():
    """Flag wrapper GMF includes whose textual closure reaches a module-owned header.

    Purview guards are inactive in a GMF (SRCTRL_MODULE_PURVIEW is defined only after
    `export module`), so such an include attaches the owned header's declarations to the
    global module -- ill-formed against an import of the owner, and clang diagnoses it
    lazily in whatever sibling partition first merges both BMIs (repro-gmf-attachment/).
    """
    bad = 0
    for dirpath, _, files in os.walk(SRC):
        if "cxx_modules_poc" in dirpath:
            continue
        for f in sorted(files):
            if not f.endswith(".cppm"):
                continue
            path = os.path.join(dirpath, f)
            gmf = read(path).split("export module", 1)[0]
            for inc in re.finditer(r'^#include "([^"]+)"', gmf, re.M):
                base = os.path.basename(inc.group(1))
                if base in OWNER_OVERRIDES:
                    continue
                if base in MOD:
                    print(f"GMF-OWNED   {f}: {base} is owned by {MOD[base]} -- import it instead")
                    bad += 1
                    continue
                seen, stack = set(), [base]
                while stack:
                    b = stack.pop()
                    if b in seen or b in GATED_CLEAN:
                        continue
                    seen.add(b)
                    for p in by_base.get(b, []):
                        for full in inc_re.findall(read(p)):
                            i = os.path.basename(full)
                            # Path-includes resolve first-party only by full suffix match --
                            # <clang/Basic/SourceLocation.h> must not collide with our
                            # SourceLocation.h by basename.
                            if "/" in full and not any(
                                    q.endswith(os.sep + full.replace("/", os.sep))
                                    for q in by_base.get(i, [])):
                                continue
                            if i in MOD and i not in OWNER_OVERRIDES:
                                print(f"GMF-CLOSURE {f}: {base} -> ... -> {b} -> {i} "
                                      f"(owned by {MOD[i]}) -- import {MOD[i]} instead")
                                bad += 1
                                stack.clear()
                                break
                            stack.append(i)
                        else:
                            continue
                        break
    print(f"\n{bad} GMF attachment hazard(s)" if bad else "GMF closures are module-free")
    return bad


def main(argv):
    if argv and argv[0] == "--audit-gmf":
        sys.exit(1 if audit_gmf() else 0)
    if argv and argv[0] == "--index":
        argv = argv[1:]
        db = argv.pop(0) if argv and argv[0].endswith(".db") else os.path.join(
            ROOT, "Sourcetrail.srctrl.db")
        load_index(db)
        print(f"# include closure from self-index: {db} "
              f"({sum(len(v) for v in INDEX_EDGES.values())} include edges)")
    if argv and argv[0] == "--apply":
        cmake_todo = []
        for tu in argv[1:]:
            kind, name, info = analyze(tu)
            if kind != "CONVERTIBLE":
                print(f"REFUSED {name}: {kind} -- {info}")
                continue
            headers, imports, soft = info
            if soft:
                print(f"NOTE {name}: include-only (two-entity) closures stay textual: " +
                      ", ".join(f"{b} ({'/'.join(o)})" for b, o in soft.items()))
            if "srctrl.logging" not in imports and "logging.h" in headers:
                imports = sorted(set(imports) | {"srctrl.logging"})
            apply_conversion(tu, headers, imports)
            cmake_todo.append(tu)
            print(f"APPLIED {name}: imports {', '.join(imports)}")
        if cmake_todo:
            print("\nAdd to the owning target's CXX_SCAN_FOR_MODULES list "
                  "(under SOURCETRAIL_CXX_MODULES_ENABLED):")
            for tu in cmake_todo:
                print(f"  {os.path.relpath(tu, ROOT)}")
        return

    tus = argv or [os.path.join(dp, f)
                   for dp, _, fs in os.walk(SRC)
                   for f in fs if f.endswith(".cpp") and "cxx_modules_poc" not in dp]
    blockers = Counter()
    for tu in tus:
        kind, name, info = analyze(tu)
        if kind == "CONVERTIBLE":
            headers, imports, soft = info
            block = "".join(f"import {m};\n" for m in imports)
            extra = ("\n  include-only kept textual: " +
                     ", ".join(f"{b} ({'/'.join(o)})" for b, o in soft.items())) if soft else ""
            print(f"CONVERTIBLE {name}\n  guard: {' '.join(headers)}\n  " +
                  block.replace("\n", "\n  ").rstrip() + extra)
        elif kind == "BLOCKED":
            for b, why in info.items():
                blockers[b] += 1
            print(f"BLOCKED     {name}: " + "; ".join(f"{b} ({w})" for b, w in info.items()))
    if blockers:
        print("\n== blockers by gated-TU count ==")
        for b, n in blockers.most_common(15):
            print(f"{n:4d}  {b}")


if __name__ == "__main__":
    main(sys.argv[1:])
