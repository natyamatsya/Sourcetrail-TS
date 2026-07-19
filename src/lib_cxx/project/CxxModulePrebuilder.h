#ifndef CXX_MODULE_PREBUILDER_H
#define CXX_MODULE_PREBUILDER_H

#include <set>
#include <string>
#include <vector>

#include "FilePath.h"

// Phase-2 driver support for C++20 named modules in source groups that don't get module flags from
// a build system (the plain C++ Source Group). Before indexing, it:
//   1. scans every source file with `clang-scan-deps -format=p1689` to learn which module each file
//      provides and which it requires,
//   2. topologically orders the module-interface units by that dependency graph (Kahn's), and
//   3. builds each BMI (`.pcm`) into a cache directory in that order,
// then hands back the flags the indexer needs so libclang can resolve `import`s against the cache
// (`-fprebuilt-module-path=<cache>`, plus `-x c++-module` for the interface units).
//
// Tools are located via the SOURCETRAIL_CLANG_BINDIR env var, else the PATH. If they are missing,
// no module unit is found, or a build fails, it degrades gracefully (anyModules stays false / the
// affected BMI is skipped) so a project without modules is never affected.
//
// NOTE (productization): this shells out to the clang/clang-scan-deps binaries and matches libclang
// by forcing the host target triple. Generating BMIs in-process via a libTooling FrontendAction (as
// GeneratePCHAction already does for PCHs) would remove the external-binary dependency and the
// flag-matching hazard; see context notes.
class CxxModulePrebuilder
{
public:
	struct Result
	{
		bool anyModules = false;
		// Flags to append to every translation unit of the source group so libclang can load the
		// prebuilt BMIs (module path + the target/builtin flags the BMIs were built with).
		std::vector<std::string> sharedFlags;
		// The module-interface units, which must additionally be indexed as `-x c++-module`.
		std::set<FilePath> interfaceUnits;
	};

	static Result prebuild(
		const std::set<FilePath>& sourceFiles,
		const std::vector<std::string>& baseCompilerFlags,
		const FilePath& cacheDir);
};

#endif	  // CXX_MODULE_PREBUILDER_H
