const std = @import("std");

// Builds `sourcetrail_zig_indexer`, a drop-in peer of the Rust/Swift indexers.
// Phase 3a (this milestone) is a standalone std.zig.Ast symbol extractor with
// unit tests; the thoth-ipc transport + flatcc wire codec (Phase 2) and ZLS
// semantic pass (Phase 3b) are layered on later. See
// context/ROADMAP_ZIG_INDEXER.md.
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const mod = b.createModule(.{
        .root_source_file = b.path("src/root.zig"),
        .target = target,
        .optimize = optimize,
    });

    const exe = b.addExecutable(.{
        .name = "sourcetrail_zig_indexer",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{.{ .name = "indexer", .module = mod }},
        }),
    });
    b.installArtifact(exe);

    const run = b.addRunArtifact(exe);
    run.step.dependOn(b.getInstallStep());
    if (b.args) |args| run.addArgs(args);
    b.step("run", "Run the Zig indexer").dependOn(&run.step);

    const tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/tests.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{.{ .name = "indexer", .module = mod }},
        }),
    });
    const run_tests = b.addRunArtifact(tests);
    b.step("test", "Run the indexer unit tests").dependOn(&run_tests.step);
}
