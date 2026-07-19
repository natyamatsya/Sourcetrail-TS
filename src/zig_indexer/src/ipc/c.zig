//! flatcc-generated C bindings for the Sourcetrail IPC FlatBuffers schemas,
//! plus the flatcc builder runtime, surfaced to Zig via @cImport. The include
//! paths (the generated dir + the flatcc runtime headers) are supplied by
//! build.zig / CMake. Link against libflatccrt.
pub const c = @cImport({
    @cInclude("flatcc/flatcc_builder.h");
    @cInclude("indexer_command_reader.h");
    @cInclude("indexer_command_builder.h");
    @cInclude("indexer_command_verifier.h");
    @cInclude("intermediate_storage_reader.h");
    @cInclude("intermediate_storage_builder.h");
    @cInclude("intermediate_storage_verifier.h");
    @cInclude("indexing_status_reader.h");
    @cInclude("indexing_status_builder.h");
});
