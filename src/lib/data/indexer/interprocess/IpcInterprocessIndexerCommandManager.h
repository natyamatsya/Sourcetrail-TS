#ifndef IPC_INTERPROCESS_INDEXER_COMMAND_MANAGER_H
#define IPC_INTERPROCESS_INDEXER_COMMAND_MANAGER_H

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "IpcSharedMemory.h"
#include "IndexerCommandType.h"
#include "ProcessId.h"

class IndexerCommand;

class IpcInterprocessIndexerCommandManager
{
public:
	IpcInterprocessIndexerCommandManager(
		const std::string& instanceUuid, ProcessId processId, bool isOwner);
	~IpcInterprocessIndexerCommandManager();

	void pushIndexerCommands(const std::vector<std::shared_ptr<IndexerCommand>>& indexerCommands);

	// Pop the next command, optionally skipping commands of a specific type.
	// Pass IndexerCommandType::INDEXER_COMMAND_UNKNOWN (default) to accept any type.
	std::shared_ptr<IndexerCommand> popIndexerCommand(
		IndexerCommandType skipType = IndexerCommandType::INDEXER_COMMAND_UNKNOWN);
	std::shared_ptr<IndexerCommand> popIndexerCommand(
		const std::set<IndexerCommandType>& skipTypes);

	bool hasIndexerCommandType(IndexerCommandType type);

	void clearIndexerCommands();
	size_t indexerCommandCount();

private:
	static const char* s_sharedMemoryNamePrefix;

	std::string m_instanceUuid;
	ProcessId m_processId;
	IpcSharedMemory m_shm;
};

#endif // IPC_INTERPROCESS_INDEXER_COMMAND_MANAGER_H
