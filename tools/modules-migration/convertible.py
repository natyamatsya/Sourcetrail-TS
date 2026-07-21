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

Output per TU: CONVERTIBLE with the exact import block to paste, BLOCKED with the dirty
headers (ranked blocker list at the end), or SKIP reasons (definer/stdexec/no modular deps).
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
            purview = text.split("SRCTRL_MODULE_PURVIEW", 1)
            if len(purview) < 2:
                continue
            for inc in re.finditer(r'^#include "([^"]+)"', purview[1], re.M):
                surface[os.path.basename(inc.group(1))] = module
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


def dirty_fwd_decls(path):
    return [n for n in fwd_re.findall(read(path)) if f"{n}.h" in MOD]


@lru_cache(maxsize=None)
def closure_verdict(base):
    """None if clean; else a short reason string."""
    if base in GATED_CLEAN:
        return None
    if base in MOD:
        return f"includes modularized {base}"
    seen, stack = set(), [base]
    while stack:
        b = stack.pop()
        if b in seen:
            continue
        seen.add(b)
        if b in STDEXEC_MARKERS:
            return "stdexec in closure (scanner-unconvertible)"
        for p in by_base.get(b, []):
            fwd = dirty_fwd_decls(p)
            if fwd:
                return f"{b}: unguarded fwd decl of attached type {fwd[0]}"
            for i in direct_includes(p):
                if i in GATED_CLEAN or i in seen:
                    continue
                if i in MOD:
                    return f"{b} -> modularized {i}"
                if i in STDEXEC_MARKERS:
                    return "stdexec in closure (scanner-unconvertible)"
                stack.append(i)
    return None


def analyze(tu):
    name = os.path.basename(tu)
    stem = name[:-4]
    if f"{stem}.h" in MOD:
        return ("SKIP", name, f"definer TU of modularized {stem}.h (classic seam)")
    incs = [b for b in direct_includes(tu) if b != "Catch2.hpp"]
    if any(b in STDEXEC_MARKERS for b in incs):
        return ("SKIP", name, "includes stdexec directly (scanner-unconvertible)")
    modular = sorted(set(b for b in incs if b in MOD))
    if not modular:
        return ("SKIP", name, "no modularized includes")
    dirty = {}
    for b in sorted(set(incs)):
        if b in MOD:
            continue
        v = closure_verdict(b)
        if v:
            dirty[b] = v
    if dirty:
        return ("BLOCKED", name, dirty)
    imports = sorted(set(MOD[b] for b in modular))
    return ("CONVERTIBLE", name, (modular, imports))


def main(argv):
    tus = argv or [os.path.join(dp, f)
                   for dp, _, fs in os.walk(SRC)
                   for f in fs if f.endswith(".cpp") and "cxx_modules_poc" not in dp]
    blockers = Counter()
    for tu in tus:
        kind, name, info = analyze(tu)
        if kind == "CONVERTIBLE":
            headers, imports = info
            block = "".join(f"import {m};\n" for m in imports)
            print(f"CONVERTIBLE {name}\n  guard: {' '.join(headers)}\n  " +
                  block.replace("\n", "\n  ").rstrip())
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
