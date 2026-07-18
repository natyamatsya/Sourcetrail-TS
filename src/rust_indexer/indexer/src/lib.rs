// flatc's `--gen-object-api` emits `alloc::` paths (no_std + alloc convention);
// bring the always-available `alloc` crate into scope so they resolve in this
// std crate.
extern crate alloc;

pub mod ipc;
pub mod parser;
pub mod schemas;
