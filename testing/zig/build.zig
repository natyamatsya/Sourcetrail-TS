const std = @import("std");

// Minimal build.zig so the Zig indexer treats this directory as the project
// root (SourceGroupZig resolves the working directory to the nearest build.zig,
// which lets the subprocess resolve cross-file @import).
pub fn build(b: *std.Build) void {
    _ = b;
}
