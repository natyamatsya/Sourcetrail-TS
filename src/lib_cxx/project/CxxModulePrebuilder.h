#ifndef CXX_MODULE_PREBUILDER_H
#define CXX_MODULE_PREBUILDER_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include "FilePath.h"

// Driver support for C++20 named modules in source groups that don't already get module flags from a
// build system. This is the thin main-process side: it cheap-detects candidate module files (a text
// read, no parsing), then hands the heavy parse-arbitrary-user-code work -- the dependency scan and
// the BMI builds -- to a crash-isolated sourcetrail_indexer subprocess (see CxxModulePrebuildRunner
// and context/DESIGN_MODULE_PREBUILD.md). It returns the flags the indexer needs so libclang can
// resolve `import`s against the built cache.
//
// Flags are supplied per file (each compile-database command carries its own); the plain C++ Source
// Group just maps every file to the same shared set. A missing/failed subprocess degrades gracefully
// (anyModules stays false), so a project without modules is never affected.
class CxxModulePrebuilder
{
public:
	struct Result
	{
		bool anyModules = false;
		// Flags to append to every translation unit of the source group so libclang can load the
		// prebuilt BMIs (the `-fprebuilt-module-path=<cache>`).
		std::vector<std::string> sharedFlags;
		// The module-interface units, which must additionally be indexed as `-x c++-module`.
		std::set<FilePath> interfaceUnits;
	};

	// `fileFlags` maps each source file to the compiler flags it is (or would be) compiled with; the
	// BMI of a module-interface unit is built with that file's own flags.
	static Result prebuild(
		const std::map<FilePath, std::vector<std::string>>& fileFlags, const FilePath& cacheDir);
};

#endif	  // CXX_MODULE_PREBUILDER_H
