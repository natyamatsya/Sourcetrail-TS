//! Sourcetrail agent MCP bridge — core library.
//!
//! Layered so the wire/transport half is verifiable without the MCP SDK:
//!   * [`schema`]   — FlatBuffers bindings (generated from the shared `*.fbs`).
//!   * [`protocol`] — channel names + `Command` builders + envelope decoders
//!                    (FlatBuffers ⇄ `serde_json`).
//!   * [`ipc`]      — [`ipc::Bridge`]: connects the four thoth-ipc routes and
//!                    exposes act-and-observe operations with request/reply
//!                    correlation.
//!
//! The `mcp` feature (see `bin/server.rs`) wraps [`ipc::Bridge`] in an `rmcp`
//! stdio server; the default build exposes only the core + a smoke-test binary.
//!
//! Design: `context/DESIGN_AGENT_MCP_BRIDGE.md`.

// FlatBuffers bindings (generated; see build.rs). The schemas are merged into one
// self-contained schema and generated as a single `schema` module, so all types
// live under `crate::schema::sourcetrail::agent`.
include!(concat!(env!("OUT_DIR"), "/schema_gen.rs"));

pub mod protocol;
pub mod ipc;

pub use ipc::Bridge;
