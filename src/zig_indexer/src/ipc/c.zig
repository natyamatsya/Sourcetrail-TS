//! flatcc-generated C bindings for the Sourcetrail IPC FlatBuffers schemas,
//! plus the flatcc builder runtime. Zig 0.17 removed @cImport, so build.zig
//! translates include/flatcc_bindings.h with a TranslateC step and hands the
//! result in as the "flatcc_c" module (include paths: our shim dir, the
//! generated dir, the flatcc runtime headers). Link against libflatccrt.
pub const c = @import("flatcc_c");
