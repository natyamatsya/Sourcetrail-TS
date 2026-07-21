#!/usr/bin/env python3
"""Dual-build-ify a settings-family header:
- add #include "SrctrlModule.h" after the include guard
- wrap the contiguous include block in #ifndef SRCTRL_MODULE_PURVIEW
- prefix top-level `class X` / `enum class X` and namespace-scope free-function
  declarations with SRCTRL_EXPORT
- guard bare fwd decls of classes (they are all module-attached family types here)
"""
import re, sys

for path in sys.argv[1:]:
    lines = open(path).read().split("\n")
    out = []
    i = 0
    # copy up to and including the #define guard line
    while i < len(lines):
        out.append(lines[i])
        if lines[i].startswith("#define ") and i > 0 and lines[i - 1].startswith("#ifndef "):
            i += 1
            break
        i += 1
    out.append("")
    out.append('#include "SrctrlModule.h"')
    # gather rest
    rest = lines[i:]
    # wrap include block(s): find contiguous runs of #include (+blank lines between)
    text = "\n".join(rest)
    def wrap_includes(m):
        return "#ifndef SRCTRL_MODULE_PURVIEW\n" + m.group(0).strip("\n") + "\n#endif\n"
    text = re.sub(r'(?:^#include [^\n]+\n(?:\n(?=#include))?)+', wrap_includes, text, count=1, flags=re.M)
    # guard bare fwd decls
    text = re.sub(r'^((?:class|struct) \w+;\n(?:(?:class|struct) \w+;\n)*)',
                  r'#ifndef SRCTRL_MODULE_PURVIEW\n\1#endif\n', text, flags=re.M)
    # export class/enum definitions at column 0
    text = re.sub(r'^(class \w+[^;]*$)', r'SRCTRL_EXPORT \1', text, flags=re.M)
    text = re.sub(r'^(enum class \w+)', r'SRCTRL_EXPORT \1', text, flags=re.M)
    # export free function declarations at column 0 (ret type + name(...) ...;) single line
    text = re.sub(r'^([A-Za-z_][\w:<>&\s\*]*\s\w+\([^;{]*\);)$', r'SRCTRL_EXPORT \1', text, flags=re.M)
    open(path, "w").write("\n".join(out) + "\n" + text)
    print("swept", path)
