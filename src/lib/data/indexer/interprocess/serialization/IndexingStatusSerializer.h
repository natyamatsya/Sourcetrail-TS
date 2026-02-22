#ifndef INDEXING_STATUS_SERIALIZER_H
#define INDEXING_STATUS_SERIALIZER_H

#include <cstdint>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "FilePath.h"

namespace IpcSerializer
{

struct IndexingStatusData
{
	std::vector<std::string> indexingFilePaths;
	std::vector<std::pair<std::size_t, std::string>> currentFiles;
	std::vector<std::string> crashedFilePaths;
	std::vector<std::size_t> finishedProcessIds;
	bool indexingInterrupted = false;
	bool queueStopped = false;
};

flatbuffers::DetachedBuffer serializeIndexingStatus(const IndexingStatusData& status);
IndexingStatusData deserializeIndexingStatus(const uint8_t* buf, std::size_t len);

}

#endif // INDEXING_STATUS_SERIALIZER_H
