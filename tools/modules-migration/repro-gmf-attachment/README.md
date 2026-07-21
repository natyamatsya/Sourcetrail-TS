# Reproducer: GMF textual parse of a module-owned header vs. import of its owner

This is the minimal reproducer for the failure that parked the `srctrl.messaging`
slice (`messaging-slice` bookmark): building `srctrl_cxx-frontend.cppm` failed with

```
error: declaration 'trim' attached to named module 'srctrl.utility:string'
       cannot be attached to other modules
note: utilityString.h:51: also found
```

with **zero** textual parses of `utilityString.h` in the failing TU (`-H`/`-E` clean).

## The actual cause (not a toolchain bug)

`srctrl_cxx-tooling.cppm` pre-loaded `MessageStatus.h` in its **global module
fragment** (playbook rule 9 -- it was a classic-with-cpp header when that wrapper was
written). The messaging slice made MessageStatus a member of `srctrl.messaging`: its
header now ends with `MessageStatus.inl`, which includes `utilityString.h` guarded by
`#ifndef SRCTRL_MODULE_PURVIEW`. **In a GMF that macro is not yet defined**, so the
guard is inactive and `utilityString.h`/`.inl` were textually parsed into
`srctrl.cxx:tooling`'s global module. The same functions arrive module-attached via
`import srctrl.utility` -- the same entity attached to both the global module and
`srctrl.utility:string` is ill-formed ([basic.link]/10-11).

## What this repro shows

Model: module `a` (partition `a:s`) owns header `s.h` + `s.inl` (inline definition of
`utility::trim`) via the dual-build EXPORT-macro pattern; module `m` has partitions
`m:t` (the carrier) and `m:f` (the sibling that fails), mirroring
`srctrl.cxx:tooling` / `srctrl.cxx:frontend`.

clang 22.1.8 diagnoses the violation **lazily and shape-dependently** -- all three
legs are required before it fires:

1. the carrier partition textually parses the header in its GMF **and** keeps the
   declarations alive (uses them), and
2. the carrier **also imports the owner module** `a`, and
3. a **sibling partition** imports both the carrier and `a`.

Contrast cases in `run.sh` that are equally ill-formed but accepted silently:
a plain (non-partition) consumer of both BMIs (`c4.cppm`), and the carrier without
`import a` (`m_t2.cppm`). This laziness is why the real failure looked like a
BMI-merge compiler bug: the diagnostic appeared far from the offending TU, which
itself compiled clean.

## The law this adds to the playbook

**A wrapper GMF may only textually include headers whose whole include closure is
module-free.** When a classic GMF dependency later joins a module, every wrapper that
pre-loaded it must switch to importing the owning module (that was the fix here:
`srctrl_cxx-tooling.cppm` dropped `#include "MessageStatus.h"` and gained
`import srctrl.messaging;`). Purview-guards (`#ifndef SRCTRL_MODULE_PURVIEW`) do not
protect GMF parses -- the macro is only defined after `export module`.

Run `./run.sh` (env `CXX=` to point at another clang). Expected: everything passes
except the final `m_f.cppm` compile, which emits the exact production diagnostic.
