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
};

#endif	  // REFRESH_INFO_H
