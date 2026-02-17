#include "IpcInterprocessIndexerCommandManager.h"

#include <cstring>

#include "IndexerCommand.h"
#include "IndexerCommandSerializer.h"
#include "logging.h"

const char* IpcInterprocessIndexerCommandManager::s_sharedMemoryNamePrefix = "icmd_ipc_";

IpcInterprocessIndexerCommandManager::IpcInterprocessIndexerCommandManager(
	const std::string& instanceUuid, ProcessId processId, bool isOwner)
	: m_instanceUuid{instanceUuid}
	, m_processId{processId}
	, m_shm{
		  s_sharedMemoryNamePrefix + instanceUuid,
		  1048576,
		  isOwner ? IpcSharedMemory::CREATE_AND_DELETE : IpcSharedMemory::OPEN_OR_CREATE}
{
}

IpcInterprocessIndexerCommandManager::~IpcInterprocessIndexerCommandManager() = default;

void IpcInterprocessIndexerCommandManager::pushIndexerCommands(
	const std::vector<std::shared_ptr<IndexerCommand>>& indexerCommands)
{
	// Deserialize existing queue, append, re-serialize
	std::vector<std::shared_ptr<IndexerCommand>> all;

	{
		IpcSharedMemory::ScopedAccess access(&m_shm);
		std::size_t len = 0;
		const uint8_t* buf = access.read(&len);

		// Check if there's existing data (first 4 bytes non-zero as FlatBuffer prefix)
		if (len >= 4 && std::memcmp(buf, "\0\0\0\0", 4) != 0)
			all = IpcSerializer::deserializeIndexerCommands(buf, len);
	}

	all.insert(all.end(), indexerCommands.begin(), indexerCommands.end());

	auto fbBuf = IpcSerializer::serializeIndexerCommands(all);

	IpcSharedMemory::ScopedAccess access(&m_shm);
	access.write(fbBuf.data(), fbBuf.size());

	LOG_INFO(access.logString());
}

std::shared_ptr<IndexerCommand> IpcInterprocessIndexerCommandManager::popIndexerCommand()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t len = 0;
	const uint8_t* buf = access.read(&len);

	if (len < 4 || std::memcmp(buf, "\0\0\0\0", 4) == 0)
		return nullptr;

	auto all = IpcSerializer::deserializeIndexerCommands(buf, len);
	if (all.empty())
		return nullptr;

	auto result = all.front();
	all.erase(all.begin());

	if (all.empty())
	{
		// Clear the buffer
		uint8_t zero[4] = {0, 0, 0, 0};
		access.write(zero, 4);
	}
	else
	{
		auto fbBuf = IpcSerializer::serializeIndexerCommands(all);
		access.write(fbBuf.data(), fbBuf.size());
	}

	return result;
}

void IpcInterprocessIndexerCommandManager::clearIndexerCommands()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	uint8_t zero[4] = {0, 0, 0, 0};
	access.write(zero, 4);
}

size_t IpcInterprocessIndexerCommandManager::indexerCommandCount()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t len = 0;
	const uint8_t* buf = access.read(&len);

	if (len < 4 || std::memcmp(buf, "\0\0\0\0", 4) == 0)
		return 0;

	auto all = IpcSerializer::deserializeIndexerCommands(buf, len);
	return all.size();
}
