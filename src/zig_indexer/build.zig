const std = @import("std");

// Builds `sourcetrail_zig_indexer`, a drop-in peer of the Rust/Swift indexers.
// Phase 3a (this milestone) is a standalone std.zig.Ast symbol extractor with
// unit tests; the thoth-ipc transport + flatcc wire codec (Phase 2) and ZLS
// semantic pass (Phase 3b) are layered on later. See
// context/ROADMAP_ZIG_INDEXER.md.
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Pure, portable core (parser + storage + wire framing) — the module the
    // unit tests exercise. No thoth-ipc / libc / flatcc dependency.
    const mod = b.createModule(.{
        .root_source_file = b.path("src/root.zig"),
        .target = target,
        .optimize = optimize,
    });

    // The thoth-ipc Zig port (ShmHandle / Mutex / makeShmName). macOS-only,
    // needs libc for shm_open/mmap.
    const thoth = b.dependency("thoth_ipc", .{
        .target = target,
        .optimize = optimize,
    }).module("thoth-ipc");

    // ZLS (0.16.0) as a library: its Analyser + DocumentStore drive cross-file
    // semantic resolution (Phase 3b). Consumed as the "zls" module it exposes.
    const zls = b.dependency("zls", .{
        .target = target,
        .optimize = optimize,
    }).module("zls");

    // flatcc C bindings for the shared .fbs schemas. gen_flatcc.sh strips
    // non-ASCII (flatcc's parser rejects it) and runs flatcc into an output dir.
    const flatcc_prefix = b.option([]const u8, "flatcc-prefix", "flatcc install prefix") orelse "/opt/homebrew";
    const schema_dir = b.option([]const u8, "schema-dir", "IPC .fbs schema dir") orelse
        "../../src/lib/data/indexer/interprocess/schemas";
    const gen = b.addSystemCommand(&.{"bash"});
    gen.addFileArg(b.path("tools/gen_flatcc.sh"));
    gen.addArg(schema_dir);
    const gen_dir = gen.addOutputDirectoryArg("flatcc");

    const exe = b.addExecutable(.{
        .name = "sourcetrail_zig_indexer",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
            .imports = &.{
                .{ .name = "indexer", .module = mod },
                .{ .name = "thoth-ipc", .module = thoth },
                .{ .name = "zls", .module = zls },
            },
        }),
    });
    configureFlatcc(exe.root_module, gen_dir, flatcc_prefix);
    b.installArtifact(exe);

    const run = b.addRunArtifact(exe);
    run.step.dependOn(b.getInstallStep());
    if (b.args) |args| run.addArgs(args);
    b.step("run", "Run the Zig indexer").dependOn(&run.step);

    const test_step = b.step("test", "Run the indexer unit tests");

    // Pure core tests (parser/storage) — no flatcc/thoth-ipc.
    const tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/tests.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{.{ .name = "indexer", .module = mod }},
        }),
    });
    test_step.dependOn(&b.addRunArtifact(tests).step);

    // IPC tests: FlatBuffers wire round-trip + storage-queue framing (needs flatcc).
    const ipc_tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/ipc/tests.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
            .imports = &.{
                .{ .name = "indexer", .module = mod },
                .{ .name = "thoth-ipc", .module = thoth },
            },
        }),
    });
    configureFlatcc(ipc_tests.root_module, gen_dir, flatcc_prefix);
    test_step.dependOn(&b.addRunArtifact(ipc_tests).step);

    // Semantic-pass tests: drive a real ZLS Session over an in-memory document
    // (needs the zls module; no flatcc/thoth-ipc).
    const semantic_tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/semantic_tests.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
            .imports = &.{
                .{ .name = "indexer", .module = mod },
                .{ .name = "zls", .module = zls },
            },
        }),
    });
    test_step.dependOn(&b.addRunArtifact(semantic_tests).step);
}

/// Add the flatcc-generated include dir, the flatcc runtime headers/lib, and
/// the libflatccrt link to a module that @cImports the generated bindings.
fn configureFlatcc(mod: *std.Build.Module, gen_dir: std.Build.LazyPath, prefix: []const u8) void {
    const b = mod.owner;
    // Our unaligned-safe flatcc accessor shim must be found before flatcc's own
    // headers (it defines the FLATCC_ACCESSORS include guard).
    mod.addIncludePath(b.path("include"));
    mod.addIncludePath(gen_dir);
    mod.addIncludePath(.{ .cwd_relative = b.pathJoin(&.{ prefix, "include" }) });
    mod.addLibraryPath(.{ .cwd_relative = b.pathJoin(&.{ prefix, "lib" }) });
    mod.linkSystemLibrary("flatccrt", .{});
}
