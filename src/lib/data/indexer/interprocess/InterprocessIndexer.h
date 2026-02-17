#ifndef INTERPROCESS_INDEXER_H
#define INTERPROCESS_INDEXER_H

#include "InterprocessBackend.h"

class InterprocessIndexer
{
public:
	InterprocessIndexer(const std::string& uuid, ProcessId processId);

	void work();

private:
	IndexerCommandManagerImpl m_interprocessIndexerCommandManager;
	IndexingStatusManagerImpl m_interprocessIndexingStatusManager;
	IntermediateStorageManagerImpl m_interprocessIntermediateStorageManager;

	const std::string m_uuid;
	const ProcessId m_processId;
};

#endif	  // INTERPROCESS_INDEXER_H
