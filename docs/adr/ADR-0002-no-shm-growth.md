# ADR-0002: Shared-memory segments do not grow — portable code chunks oversized payloads

- **Status:** Accepted
- **Date:** 2026-07-10
- **Deciders:** natyamatsya
- **Related:** chunked storage pushes (commit `6d0be00755`), thoth-ipc shared memory
  (Rust `libipc::ShmHandle`, C++ `ipc::shm::handle`),
  `src/lib/utility/interprocess/IpcSharedMemory`,
  `src/rust_indexer/indexer/src/ipc/{shm,storage}.rs`.

## Context

Indexer results travel from subprocesses to the app through named shared-memory queues
(`IpcInterprocessIntermediateStorageManager`, initial segment size 16 MiB). A full crate index can
exceed that comfortably — rust-analyzer (843 files, ~210k source locations) serializes to well over
16 MiB. The original design let the writer **grow** the segment on demand: write a
`needed_capacity` request into the queue header, call `grow()`, and let the reader re-map.

Growing a shared-memory object turns out to be a **platform capability, not a portable
operation**:

| platform | mechanism | resizable? |
|---|---|---|
| Linux | `ftruncate` on a POSIX shm object enlarges it; re-mapping picks up the new size | **yes** |
| macOS | XNU (`bsd/kern/posix_shm.c`, `pshm_truncate`) accepts exactly **one** sizing `ftruncate` per shm object; every later call fails with `EINVAL`. This is undocumented in Apple's man pages and was verified empirically. | **no** |
| Windows | a section's size is fixed at `CreateFileMapping`; no resize API exists | **no** |

So `grow()` worked on exactly one of three platforms. On macOS both the Rust and the C++
implementations re-acquired the same name and died on the second `ftruncate` (a leaked `EINVAL`;
macOS can additionally zero the object's contents on that failed path). In the app pipeline this
killed large-crate indexing: the subprocess errored out right after signalling `needed_capacity`,
the parent's own grow attempt threw, and the payload never arrived — the run stalled at the
initial segment size. Commit `6d0be00755` fixed the data path by chunking.

## Decision

1. **Shared-memory segments are fixed-size in portable code.** Any payload that can exceed the
   segment is **chunked by the writer**: the Rust indexer splits results into self-contained
   `IntermediateStorage` queue entries (`OwnedIntermediateStorage::chunks()`, ~7 MiB estimated
   per entry; the `storage_count() < 2` back-pressure keeps at most two entries queued). The
   indexer chunks **unconditionally on every platform** — the portable path never depends on
   growth. A payload that still does not fit is a hard error, not a grow request.

2. **`grow()` remains, as a Linux-only capability.** thoth-ipc keeps the working Linux
   implementation. Where growth is impossible (macOS, Windows) `grow()` fails fast with an
   explicit, documented "unsupported on this platform" error instead of leaking `EINVAL` out of
   a doomed `ftruncate`.

3. **Callers must check `can_grow()` before relying on growth.** Both implementations expose the
   capability query — Rust: `ShmHandle::can_grow()` / `PlatformShm::can_grow()`; C++:
   `ipc::shm::handle::can_grow()`, surfaced in the app as `IpcSharedMemory::canGrow()`. That is
   the sanctioned way to use growth: branch up front instead of try-and-fail; growth-dependent
   code (the app's `growIfNeeded()`) is gated on it. Portable code chunks regardless.

4. **The wire format is unchanged.** The storage-queue header keeps its
   `[u64 needed_capacity][u32 count]` layout. The Rust indexer always writes `0`; the C++ side
   keeps reading the field but only acts on it where `canGrow()` holds.

## Consequences

**Positive**
- Large-crate indexing works on all three platforms at any payload size; the 16 MiB segment is
  sufficient by construction (chunks + back-pressure).
- Failure modes are explicit: an oversized entry is a clear "must be chunked" error, an
  unsupported grow is a clear capability error — no undocumented `EINVAL`, no zeroed segments.
- Capability is queryable (`can_grow()`), so platform differences are a branch, not a crash.

**Negative / costs**
- Chunking costs work and memory on the writer (row cloning per chunk, repeated node/file stub
  rows that the reader's inject step dedups).
- Linux-only code *could* rely on growth but must not in any shared path — a discipline rule
  guarded by the capability query.

**Neutral**
- The `needed_capacity` header field is vestigial on non-Linux platforms but stays for wire
  compatibility.
- C++ indexer subprocesses push one small TU per command and were never close to the limit;
  their push path keeps the (now clearer) hard error if that ever changes.

## References
- `docs/adr/ADR-0001-expected-error-channel.md` — errors-as-values convention this follows.
- XNU source: `bsd/kern/posix_shm.c` (`pshm_truncate`) — the one-shot `ftruncate` rule.
- Commit `6d0be00755` — fix(rust-indexer): chunk oversized IPC storage pushes.
- `src/rust_indexer/indexer/src/ipc/storage.rs` — chunker and queue writer.
- `submodules/thoth-ipc` — `rust/libipc/src/{shm.rs,platform/{posix,windows}.rs}`,
  `cpp/libipc/{include/libipc/shm.h,src/libipc/shm.cpp}` — `grow()`/`can_grow()`.
