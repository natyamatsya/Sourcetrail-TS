#ifndef IPC_INTERPROCESS_INDEXER_COMMAND_MANAGER_H
#define IPC_INTERPROCESS_INDEXER_COMMAND_MANAGER_H

#include <memory>
#include <string>
#include <vector>

#include "IpcSharedMemory.h"
#include "ProcessId.h"

class IndexerCommand;

class IpcInterprocessIndexerCommandManager
{
public:
	IpcInterprocessIndexerCommandManager(
		const std::string& instanceUuid, ProcessId processId, bool isOwner);
	~IpcInterprocessIndexerCommandManager();

	void pushIndexerCommands(const std::vector<std::shared_ptr<IndexerCommand>>& indexerCommands);
	std::shared_ptr<IndexerCommand> popIndexerCommand();

	void clearIndexerCommands();
	size_t indexerCommandCount();

private:
	static const char* s_sharedMemoryNamePrefix;

	std::string m_instanceUuid;
	ProcessId m_processId;
	IpcSharedMemory m_shm;
};

#endif // IPC_INTERPROCESS_INDEXER_COMMAND_MANAGER_H
