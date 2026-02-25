#ifndef INTERPROCESS_INDEXER_H
#define INTERPROCESS_INDEXER_H

#include "InterprocessBackend.h"

#include <expected>

class InterprocessIndexer
{
public:
	using WorkResult = std::expected<void, std::string>;

	InterprocessIndexer(const std::string& uuid, ProcessId processId);

	WorkResult work();

private:
	IndexerCommandManagerImpl m_interprocessIndexerCommandManager;
	IndexingStatusManagerImpl m_interprocessIndexingStatusManager;
	IntermediateStorageManagerImpl m_interprocessIntermediateStorageManager;

	const std::string m_uuid;
	const ProcessId m_processId;
};

#endif	  // INTERPROCESS_INDEXER_H
