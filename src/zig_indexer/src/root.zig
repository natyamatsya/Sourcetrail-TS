//! Public surface of the Zig indexer core (imported as `indexer`).
pub const storage = @import("storage.zig");
pub const parser = @import("parser.zig");
pub const chunker = @import("chunker.zig");

pub const Storage = storage.Storage;
pub const Chunk = storage.Chunk;
pub const indexSource = parser.indexSource;
