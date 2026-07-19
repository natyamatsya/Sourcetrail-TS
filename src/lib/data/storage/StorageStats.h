#ifndef STORAGE_STATS_H
#define STORAGE_STATS_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstddef>

#include "TimeStamp.h"
#endif

SRCTRL_EXPORT struct StorageStats
{
	StorageStats() = default;

	std::size_t nodeCount = 0;
	std::size_t edgeCount = 0;

	std::size_t fileCount = 0;
	std::size_t completedFileCount = 0;
	std::size_t fileLOCCount = 0;

	TimeStamp timestamp;
};

#endif	  // STORAGE_STATS_H
