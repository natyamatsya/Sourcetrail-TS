#ifndef INDEXING_STATUS_SERIALIZER_H
#define INDEXING_STATUS_SERIALIZER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstdint>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "FilePath.h"
#endif

namespace IpcSerializer
{

SRCTRL_EXPORT struct IndexingStatusData
{
	std::vector<std::string> indexingFilePaths;
	std::vector<std::pair<std::size_t, std::string>> currentFiles;
	std::vector<std::string> crashedFilePaths;
	std::vector<std::size_t> finishedProcessIds;
	bool indexingInterrupted = false;
	bool queueStopped = false;
};

SRCTRL_EXPORT flatbuffers::DetachedBuffer serializeIndexingStatus(const IndexingStatusData& status);
SRCTRL_EXPORT IndexingStatusData deserializeIndexingStatus(const uint8_t* buf, std::size_t len);

}

#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexingStatusSerializer.inl"
#endif

#endif // INDEXING_STATUS_SERIALIZER_H
