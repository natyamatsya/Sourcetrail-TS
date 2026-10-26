#ifndef REFRESH_INFO_H
#define REFRESH_INFO_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <set>

#include "FilePath.h"
#endif

SRCTRL_EXPORT enum class RefreshMode
{
	NONE,
	UPDATED_FILES,
	UPDATED_AND_INCOMPLETE_FILES,
	ALL_FILES
};

SRCTRL_EXPORT struct RefreshInfo
{
	std::set<FilePath> filesToIndex;
	std::set<FilePath> filesToClear;
	std::set<FilePath> nonIndexedFilesToClear;

	RefreshMode mode = RefreshMode::NONE;

	// Whether producing the indexer commands may run the work they depend on -- the C++20 module
	// prebuild, which builds every module's BMI in a subprocess. Off when the commands are only
	// inspected (their command hashes, for flag-aware refresh), which happens around every run and
	// must not rebuild the BMIs each time.
	bool buildPrerequisites = true;
};

#endif	  // REFRESH_INFO_H
