//! Public surface of the Zig indexer core (imported as `indexer`).
pub const storage = @import("storage.zig");
pub const parser = @import("parser.zig");

pub const Storage = storage.Storage;
pub const indexSource = parser.indexSource;
