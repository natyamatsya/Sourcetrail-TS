#!/usr/bin/env python3
"""Mechanical cpp -> inl for the settings family.

- Drops ALL #include lines (the Bodies aggregator supplies context).
- Prefixes namespace-scope function definitions and static-member definitions with `inline`.
- Writes X.inl next to X.cpp; does NOT delete the cpp (done after review).
"""
import re, sys, os

HEADER = """// Inline implementations for {h}. Included ONLY via SourceGroupSettingsBodies.h (classic: one
// TU emits the weak defs; module build: the srctrl.settings wrapper) -- not by the header itself,
// so the settings family's cross-references stay acyclic. All definitions inline.

#pragma once

"""

def convert(path):
    src = open(path).read()
    stem = os.path.basename(path)[:-4]
    lines = src.split("\n")
    out = []
    for i, l in enumerate(lines):
        if re.match(r'\s*#include\b', l):
            continue
        # static member definitions: `const T X::s_...` / `T X::s_...`
        if re.match(r'^(const\s+)?[\w:<>,\s&*]+\s+\w+::s_\w+\s*=', l):
            out.append("inline " + l)
            continue
        # function definition start: return type + Class::name( ... or Class::Class(
        if (re.match(r'^[\w:<>,~]+.*\w+::[~\w]+\(', l) or re.match(r'^\w[\w\s:<>,&*]*\n?$', l) == None) and re.match(r'^[A-Za-z_~]', l) and "::" in l and "(" in l and not l.startswith("using") and not l.rstrip().endswith(";"):
            out.append("inline " + l)
            continue
        # two-line defs: bare return type line followed by Class::fn( line
        if re.match(r'^[A-Za-z_][\w:<>,\s&*]*$', l) and i + 1 < len(lines) and re.match(r'^\w[\w:]*::[~\w]+\(', lines[i + 1]):
            out.append("inline " + l)
            continue
        out.append(l)
    body = "\n".join(out).lstrip("\n")
    inl = path[:-4] + ".inl"
    open(inl, "w").write(HEADER.format(h=stem + ".h") + body + ("\n" if not body.endswith("\n") else ""))
    print("wrote", inl)

for p in sys.argv[1:]:
    convert(p)
