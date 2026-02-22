# Design: CMake File-based API Integration

## Background

When Sourcetrail was originally designed, the only reliable way to extract
compile information from a CMake project was to generate a
`compile_commands.json` compilation database (CDB) and point Sourcetrail at it.
This workflow has several friction points:

- The user must manually run CMake with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.
- The CDB only covers translation units, not headers.
- There is no structured representation of targets, their dependencies, or
  their source membership — only a flat list of compile invocations.
- Project structure (which files belong to which target, what the target type
  is, what its dependencies are) is entirely lost.
- Re-indexing after a CMake reconfigure requires the user to re-point
  Sourcetrail at the regenerated CDB.

The **CMake File-based API** (introduced in CMake 3.14) solves all of these
problems. It is a first-class, stable, machine-readable interface that CMake
writes into the build tree. It exposes the full project model: targets,
sources, compile flags, include paths, link dependencies, install rules, and
more — in structured JSON.

---

## What the CMake File-based API Provides

A client writes a query file into `<build>/.cmake/api/v1/query/` before CMake
runs (or before `cmake --build`). CMake then writes reply files into
`<build>/.cmake/api/v1/reply/`. The reply is a set of versioned JSON objects:

| Object kind | Content |
| --- | --- |
| `codemodel-v2` | All targets in all configurations; source file membership; directory structure |
| `target-v2` | Per-target: sources, compile groups (flags + defines + includes per file), link libraries, dependencies |
| `cache-v2` | All CMake cache variables |
| `cmakeFiles-v1` | All `CMakeLists.txt` and included `.cmake` files (for change detection) |
| `toolchains-v1` | Compiler paths and versions per language |

The `target-v2` object is the key one. For each target it provides:

```json
{
  "name": "Sourcetrail_lib",
  "type": "STATIC_LIBRARY",
  "sources": [
    {
      "path": "src/lib/app/Application.cpp",
      "compileGroupIndex": 0,
      "isGenerated": false
    }
  ],
  "compileGroups": [
    {
      "language": "CXX",
      "compileCommandFragments": [{ "fragment": "-std=c++17 -O2" }],
      "includes": [{ "path": "/usr/include", "isSystem": true }, ...],
      "defines": [{ "define": "BUILD_RUST_LANGUAGE_PACKAGE=1" }]
    }
  ],
  "dependencies": [{ "id": "External_lib_ipc::@..." }]
}
```

This is strictly richer than `compile_commands.json`: it preserves target
identity, dependency graph, and the mapping from source file to compile group.

---

## Current Sourcetrail Workflow (CDB path)

```text
User: cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -B build .
User: (in Sourcetrail) New Project → C++ CDB → select build/compile_commands.json
Sourcetrail: reads flat list of {file, command} pairs
Sourcetrail: for each file, parses -I, -D flags from the command string
Sourcetrail: indexes each file independently
```

**Pain points:**

1. **Manual step** — user must know to pass the CMake flag.
2. **No target awareness** — Sourcetrail cannot show "which target owns this
   file" or "what are the link-time dependencies of this target".
3. **Header files missing** — headers not listed in the CDB are indexed only
   if reachable via `#include` from a TU in the CDB.
4. **Stale on reconfigure** — if the user adds a new source file and
   reconfigures, Sourcetrail does not know the CDB changed.
5. **Flag parsing is fragile** — Sourcetrail re-parses the raw compiler
   command string to extract `-I` and `-D` flags, which breaks for unusual
   compiler drivers or response files.

---

## Proposed Integration

### Phase 1 — Query file injection and reply parsing

Add a new source group type `SourceGroupCxxCMakeFileAPI` alongside the
existing `SourceGroupCxxCdb`.

**Setup:** When the user points Sourcetrail at a CMake build directory,
Sourcetrail writes the query file:

```text
<build>/.cmake/api/v1/query/client-sourcetrail/query.json
```

with content:

```json
{
  "requests": [
    { "kind": "codemodel", "version": 2 },
    { "kind": "target",    "version": 2 },
    { "kind": "cmakeFiles","version": 1 },
    { "kind": "toolchains","version": 1 }
  ]
}
```

On the next CMake configure (or `cmake --build`), CMake writes the reply.
Sourcetrail then reads the reply directory.

**New settings type:**

```cpp
class SourceGroupSettingsCxxCMakeFileAPI
    : public SourceGroupSettingsWithComponents<
          SourceGroupSettingsWithCxxPathsAndFlags,
          SourceGroupSettingsWithExcludeFilters,
          SourceGroupSettingsWithIndexedHeaderPaths>
{
    FilePath m_buildDirectory;   // path to the CMake build tree
    std::string m_targetFilter;  // optional glob, e.g. "Sourcetrail_*"
    std::string m_configuration; // "Debug" / "Release" / ""
};
```

**New source group:**

```cpp
class SourceGroupCxxCMakeFileAPI : public SourceGroup
{
    std::vector<std::shared_ptr<IndexerCommand>>
    getIndexerCommands(const RefreshInfo&) const override;

    // Reads <build>/.cmake/api/v1/reply/codemodel-v2-*.json,
    // then per-target target-v2-*.json files.
    // Returns one IndexerCommandCxx per source file, with flags
    // taken directly from the compileGroups array (no string parsing).
};
```

**New parser:**

```cpp
class CMakeFileAPIReader
{
public:
    explicit CMakeFileAPIReader(const FilePath& buildDir);

    // Returns true if a valid reply exists.
    bool hasReply() const;

    // Triggers cmake --build (or cmake .) to generate the reply if absent.
    bool ensureReply() const;

    struct SourceEntry {
        FilePath path;
        std::string language;          // "CXX", "C"
        std::vector<FilePath> includes;
        std::vector<std::string> defines;
        std::vector<std::string> compileFlags;
        std::string targetName;
        std::string targetType;        // EXECUTABLE, STATIC_LIBRARY, ...
    };

    std::vector<SourceEntry> getSources(
        const std::string& config,
        const std::string& targetGlob) const;

    // Returns all CMakeLists.txt and .cmake files (for change detection).
    std::vector<FilePath> getCMakeInputFiles() const;
};
```

### Phase 2 — Target graph as first-class nodes

The File API exposes the full dependency graph between targets. This can be
surfaced in Sourcetrail as a new node/edge kind:

| CMake concept | Sourcetrail node kind | Sourcetrail edge kind |
| --- | --- | --- |
| `EXECUTABLE` target | `NODE_FUNCTION` (or new `NODE_TARGET`) | — |
| `STATIC_LIBRARY` / `SHARED_LIBRARY` | `NODE_MODULE` | — |
| `target_link_libraries` dependency | — | `EDGE_INCLUDE` (or new `EDGE_LINK`) |
| Source file → target membership | — | `EDGE_MEMBER` |

This would allow a user to click on a target and see all its sources, all its
link dependencies, and all targets that depend on it — without having indexed
a single source file.

### Phase 3 — Automatic change detection

`cmakeFiles-v1` lists every `CMakeLists.txt` and `.cmake` file that was read
during configure. Sourcetrail can watch these files for modification and
automatically re-read the File API reply (triggering a re-index if the source
file set changes).

This replaces the current manual "re-index" workflow for structural changes
(adding/removing files, changing compile flags).

### Phase 4 — Project creation wizard

Instead of asking the user to:

1. Run CMake manually with the right flags
2. Find the `compile_commands.json` file
3. Point Sourcetrail at it

The new wizard would:

1. Ask for the source directory (where `CMakeLists.txt` lives)
2. Ask for the build directory (or offer to create one)
3. Write the query file
4. Optionally run `cmake -B <build> <source>` in the background
5. Read the reply and populate the project settings automatically

---

## Comparison: CDB vs File API

| Capability | `compile_commands.json` | CMake File API |
| --- | --- | --- |
| Compile flags per file | ✅ (raw string) | ✅ (structured) |
| Include paths | ✅ (parsed from string) | ✅ (structured array) |
| Defines | ✅ (parsed from string) | ✅ (structured array) |
| Target identity | ❌ | ✅ |
| Target dependency graph | ❌ | ✅ |
| Source → target mapping | ❌ | ✅ |
| Header files | ❌ | ✅ (via `isGenerated`, header sets) |
| CMake input file list | ❌ | ✅ |
| Toolchain info | ❌ | ✅ |
| Requires extra CMake flag | ✅ (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) | ❌ (query file is enough) |
| Requires CMake ≥ | 3.5 | 3.14 |

---

## Implementation Notes

### Reading the reply

The reply directory contains an `index-*.json` file that lists all available
reply objects with their filenames. The correct reading order is:

1. Read `index-*.json` → find `codemodel-v2` reply filename.
2. Read `codemodel-v2-*.json` → get list of target filenames.
3. For each target: read `target-v2-<name>-<hash>.json`.

All paths in the reply are relative to the build directory unless they are
absolute (external sources).

### Handling multiple configurations

The `codemodel-v2` object has a `configurations` array. For a single-config
generator (Ninja, Make) there is one entry. For multi-config (Xcode, MSVC)
there are multiple. Sourcetrail should default to the first configuration or
let the user pick.

### Dependency: JSON parser

The project already uses `tomlplusplus` for TOML parsing. For JSON, the
simplest addition is `nlohmann/json` (header-only, available in vcpkg as
`nlohmann-json`). Alternatively, the File API JSON is simple enough to parse
with a minimal hand-written reader.

---

## Open Questions

1. **Backward compatibility** — should the CDB source group type be kept as-is
   or deprecated in favour of the File API type? Recommendation: keep both;
   the CDB type is useful for non-CMake build systems that emit
   `compile_commands.json` (Bazel, Meson, etc.).

2. **Query file timing** — the query file must exist before CMake runs. If the
   user has already configured their build tree without the query file,
   Sourcetrail must trigger a reconfigure. This can be done with
   `cmake --build <dir> --target cmake_check_build_system` or a bare
   `cmake <dir>`.

3. **Target node kind** — should CMake targets be first-class Sourcetrail
   nodes? This requires a new `NodeKind` value and corresponding UI treatment.
   A conservative first step is to store target name as a node attribute and
   expose it in the code view tooltip.

4. **Generated files** — the File API marks generated sources
   (`isGenerated: true`). These should be indexed but their paths may not
   exist until after a build. Sourcetrail should skip them during pre-build
   indexing and re-index after a build completes.
