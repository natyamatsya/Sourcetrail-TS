# MSVC findings — GMF-attachment reproducer (handoff step W1, Risk 0/1)

Result of porting `run.sh` to `run-msvc.ps1` and running it on Windows. This is the risk-1
attachment smoke test from
`docs/technical_notes/cxx20-modules-migration/msvc-modules-handoff.typ`.

## Environment

| | |
|---|---|
| Toolset | **MSVC 14.51.36231** (VS "18"/2026-gen; `_MSC_VER` 1951, CMake compiler version 19.51) |
| Install | VS 18 Community (default toolset), pinned via `scripts/win/Init-ModulesEnv.ps1` |
| Flags | `cl /std:c++latest /nologo /EHsc /c /interface /TP … /ifcOutput …  /reference …` |
| Date | 2026-07-22 |

## Per-step result

| Step | Shape | clang | **MSVC 14.51** |
|---|---|---|---|
| 1 | `a:s` owner (`trim` inline via `s.h`) | ok | **ok** |
| 2 | primary `a` (`export import :s`) | ok | **ok** |
| 3 | plain `b2` (GMF textual parse of `s.h`) | ok | **ok** |
| 4 | contrast 1: `c4` consumer of `a`+`b2`, uses `trim` | ok (ill-formed NDR, silent) | **ok** |
| 5 | contrast 2: carrier `m:t` **without** `import a` | ok | **ok** |
| 6 | `m:f` sibling via `m-t2` | ok | **ok** |
| 7 | carrier `m:t` **with** `import a` | ok | **ok** |
| 8 | `m:f` sibling via `m-t` — **clang rejects here** | **FAIL** `declaration 'trim' attached to named module 'a:s' cannot be attached to other modules` | **ok** |

## Interpretation

**MSVC accepts every step, including step 8, which clang rejects.** This is precisely the handoff's
**Risk 0** prediction: MSVC uses **weak module ownership** — module-attached entities mangle the same
as classic ones — so the `[basic.link]/10` attachment clash that clang's strong ownership diagnoses
as a hard error simply does not manifest. It confirms two things:

1. **Risk 1 is clear for this construct.** The dual-build attachment machinery the reproducer models
   (`export`/`extern "C++"`-attached header owned by a module, textually parsed in a sibling's GMF)
   **compiles on MSVC** with `/interface` + `/reference`. The single most MSVC-sensitive construct we
   use did not fall over.
2. **MSVC cannot be the correctness referee.** Step 8 is ill-formed per `[basic.link]/10` regardless
   of compiler; MSVC's acceptance is silence, not a blessing. Per Risk 0, **keep the strict
   global-module model unchanged on Windows and let the macOS clang build stay the semantic
   referee** — "MSVC builds it" is not evidence of correctness.

No divergence in the *accepted* steps (1–7); the only divergence is MSVC's non-rejection of the
clang-illegal step 8, which is expected and non-blocking.

## Notes carried forward (for W2+)

- **`NOMINMAX` is not actually defined anywhere in the tree** (the handoff's "already project policy"
  claim is aspirational — verified: no `#define NOMINMAX`, no compile definition;
  `setMsvcTargetOptions` in `cmake/Sourcetrail.cmake` sets `WIN32_LEAN_AND_MEAN`/`UNICODE`/`WINVER`
  but not `NOMINMAX`). Not exercised here (no `<windows.h>` in these fixtures), but it must be added
  before any real wrapper/GMF pulls in `<windows.h>` on MSVC.
- Reproducer is fixture-identical to `run.sh`; only the driver differs. Re-run any time with
  `scripts/win/Init-ModulesEnv.ps1; tools/modules-migration/repro-gmf-attachment/run-msvc.ps1`.

## How to reproduce

```powershell
.\scripts\win\Init-ModulesEnv.ps1            # pins MSVC 14.51.36231, imports the VS x64 env
.\tools\modules-migration\repro-gmf-attachment\run-msvc.ps1
```
