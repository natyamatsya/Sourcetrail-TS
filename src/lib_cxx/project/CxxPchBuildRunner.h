#ifndef CXX_PCH_BUILD_RUNNER_H
#define CXX_PCH_BUILD_RUNNER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
class FilePath;
#endif

// Runs inside the sourcetrail_indexer subprocess (invoked with --prebuild-pch=<request>). Reads the
// request JSON { pchInput, pchOutput, compilerFlags[], compilerPath }, builds the project's
// precompiled header (the .pch artifact) via GeneratePCHAction, and serializes the symbols that
// build indexes to <pchOutput>.storage for the main process to merge into the graph.
//
// The PCH build parses arbitrary user code (the header and everything it includes), so -- like the
// module prebuild -- it belongs in a crash-isolated subprocess rather than the main app process.
// See context/DESIGN_MODULE_PREBUILD.md (Phase D).
SRCTRL_EXPORT class CxxPchBuildRunner
{
public:
	static int run(const FilePath& requestPath);
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxPchBuildRunner.inl"
#endif

#endif	  // CXX_PCH_BUILD_RUNNER_H
