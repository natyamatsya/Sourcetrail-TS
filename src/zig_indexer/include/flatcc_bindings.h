/*
 * Umbrella header for the flatcc-generated Sourcetrail IPC bindings plus the
 * flatcc builder runtime. build.zig runs it through a TranslateC step (Zig
 * 0.17 removed @cImport) and src/ipc/c.zig re-exports the resulting module.
 */

/*
 * Must precede every flatcc header: defines the FLATCC_ACCESSORS guard so
 * flatcc's stock scalar accessors (direct `*(T*)p` casts, which translate-c
 * turns into alignment-checked loads that abort on real unaligned buffer
 * fields) are replaced with memcpy-based, unaligned-safe equivalents.
 */
#include "flatcc_accessors_shim.h"

#include "flatcc/flatcc_builder.h"
#include "indexer_command_reader.h"
#include "indexer_command_builder.h"
#include "indexer_command_verifier.h"
#include "intermediate_storage_reader.h"
#include "intermediate_storage_builder.h"
#include "intermediate_storage_verifier.h"
#include "indexing_status_reader.h"
#include "indexing_status_builder.h"
