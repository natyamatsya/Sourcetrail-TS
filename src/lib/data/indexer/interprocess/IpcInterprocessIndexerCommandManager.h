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

	// Block until a command is available (woken immediately by pushIndexerCommands
	// or notifyWaiters), or until timeoutMs elapses. Returns null on timeout so the
	// caller can re-check its stop conditions; the timeout is a liveness backstop --
	// a missed notify degrades to polling at that interval, never a hang.
	std::shared_ptr<IndexerCommand> popIndexerCommandBlocking(
		const std::set<IndexerCommandType>& skipTypes, uint32_t timeoutMs);

	// Wake any process blocked in popIndexerCommandBlocking (e.g. when the queue is
	// stopped or indexing is interrupted, so subprocesses exit promptly).
	void notifyWaiters();

	bool hasIndexerCommandType(IndexerCommandType type);

	void clearIndexerCommands();
	size_t indexerCommandCount();

private:
	// Pop one command from the queue using an already-held access, or null if the
	// queue holds no acceptable command. Shared by the polling and blocking pops.
	std::shared_ptr<IndexerCommand> tryPopLocked(
		IpcSharedMemory::ScopedAccess& access, const std::set<IndexerCommandType>& skipTypes);

	static const char* s_sharedMemoryNamePrefix;

	std::string m_instanceUuid;
	ProcessId m_processId;
	IpcSharedMemory m_shm;
};

#endif // IPC_INTERPROCESS_INDEXER_COMMAND_MANAGER_H
