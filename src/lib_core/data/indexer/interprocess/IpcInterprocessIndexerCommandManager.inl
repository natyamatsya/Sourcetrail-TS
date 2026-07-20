// Inline implementations for IpcInterprocessIndexerCommandManager.h. Included at the end of that
// header (classic) or via the srctrl.interprocess wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <algorithm>
#include <cstring>

#include "IndexerCommand.h"
#include "IndexerCommandSerializer.h"
#include "IndexerCommandType.h"
#include "logging.h"
#endif

inline const char* IpcInterprocessIndexerCommandManager::s_sharedMemoryNamePrefix = "icmd_ipc_";

inline IpcInterprocessIndexerCommandManager::IpcInterprocessIndexerCommandManager(
	const std::string& instanceUuid, ProcessId processId, bool isOwner)
	: m_instanceUuid{instanceUuid}
	, m_processId{processId}
	, m_shm{
		  s_sharedMemoryNamePrefix + instanceUuid,
		  64 * 1024 * 1024,
		  isOwner ? IpcSharedMemory::AccessMode::CREATE_AND_DELETE : IpcSharedMemory::AccessMode::OPEN_OR_CREATE}
{
}

inline IpcInterprocessIndexerCommandManager::~IpcInterprocessIndexerCommandManager() = default;

inline void IpcInterprocessIndexerCommandManager::pushIndexerCommands(
	const std::vector<std::shared_ptr<IndexerCommand>>& indexerCommands)
{
	using enum IpcSharedMemory::AccessMode;
	using enum IndexerCommandType;
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

	{
		IpcSharedMemory::ScopedAccess access(&m_shm);
		access.write(fbBuf.data(), fbBuf.size());

		LOG_INFO_STREAM(<< "[pid=" << static_cast<int>(m_processId) << "] pushed "
			<< indexerCommands.size() << " command(s), queue size now " << all.size()
			<< " - " << access.logString());
	}

	// Wake subprocesses blocked in popIndexerCommandBlocking now that work exists.
	m_shm.notifyAll();
}

inline void IpcInterprocessIndexerCommandManager::notifyWaiters()
{
	m_shm.notifyAll();
}

inline std::shared_ptr<IndexerCommand> IpcInterprocessIndexerCommandManager::popIndexerCommand(
	IndexerCommandType skipType)
{
	using enum IpcSharedMemory::AccessMode;
	using enum IndexerCommandType;
	if (skipType == INDEXER_COMMAND_UNKNOWN)
		return popIndexerCommand(std::set<IndexerCommandType> {});
	return popIndexerCommand(std::set<IndexerCommandType> {skipType});
}

inline std::shared_ptr<IndexerCommand> IpcInterprocessIndexerCommandManager::tryPopLocked(
	IpcSharedMemory::ScopedAccess& access,
	const std::set<IndexerCommandType>& skipTypes,
	const std::string& onlyGroupId)
{
	std::size_t len = 0;
	const uint8_t* buf = access.read(&len);

	if (len < 4 || std::memcmp(buf, "\0\0\0\0", 4) == 0)
		return nullptr;

	auto all = IpcSerializer::deserializeIndexerCommands(buf, len);
	if (all.empty())
		return nullptr;

	// Find the first command that is not of a skipped type and — when a group
	// filter is set — belongs to this consumer's source group (fan-out S2).
	auto it = std::find_if(all.begin(), all.end(), [&skipTypes, &onlyGroupId](const auto& cmd) {
		if (skipTypes.find(cmd->getIndexerCommandType()) != skipTypes.end())
			return false;
		return onlyGroupId.empty() || cmd->getSourceGroupId() == onlyGroupId;
	});
	if (it == all.end())
		return nullptr;

	auto result = *it;
	all.erase(it);

	if (all.empty())
	{
		uint8_t zero[4] = {0, 0, 0, 0};
		access.write(zero, 4);
	}
	else
	{
		auto fbBuf = IpcSerializer::serializeIndexerCommands(all);
		access.write(fbBuf.data(), fbBuf.size());
	}

	LOG_INFO_STREAM(<< "[pid=" << static_cast<int>(m_processId) << "] popped command of type "
		<< indexerCommandTypeToString(result->getIndexerCommandType())
		<< ", " << all.size() << " command(s) remaining");

	return result;
}

inline std::shared_ptr<IndexerCommand> IpcInterprocessIndexerCommandManager::popIndexerCommand(
	const std::set<IndexerCommandType>& skipTypes)
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	return tryPopLocked(access, skipTypes);
}

inline std::shared_ptr<IndexerCommand> IpcInterprocessIndexerCommandManager::popIndexerCommandBlocking(
	const std::set<IndexerCommandType>& skipTypes, uint32_t timeoutMs, const std::string& onlyGroupId)
{
	IpcSharedMemory::ScopedAccess access(&m_shm);

	// The predicate check and the wait share one continuous lock hold, so a push
	// (write + notify) cannot slip in between and be lost.
	if (std::shared_ptr<IndexerCommand> command = tryPopLocked(access, skipTypes, onlyGroupId))
		return command;

	access.wait(timeoutMs);

	// Woken by a push/notify or the timeout elapsed; try once more. If another
	// subprocess took the command, the caller loops and blocks again.
	return tryPopLocked(access, skipTypes, onlyGroupId);
}

inline bool IpcInterprocessIndexerCommandManager::hasIndexerCommandType(IndexerCommandType type)
{
	using enum IpcSharedMemory::AccessMode;
	using enum IndexerCommandType;
	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t len = 0;
	const uint8_t* buf = access.read(&len);

	if (len < 4 || std::memcmp(buf, "\0\0\0\0", 4) == 0)
		return false;

	auto all = IpcSerializer::deserializeIndexerCommands(buf, len);
	if (type == INDEXER_COMMAND_UNKNOWN)
		return !all.empty();

	return std::any_of(all.begin(), all.end(), [type](const std::shared_ptr<IndexerCommand>& command) {
		return command && command->getIndexerCommandType() == type;
	});
}

inline void IpcInterprocessIndexerCommandManager::clearIndexerCommands()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	uint8_t zero[4] = {0, 0, 0, 0};
	access.write(zero, 4);
	LOG_INFO_STREAM(<< "[pid=" << static_cast<int>(m_processId) << "] cleared all indexer commands");
}

inline size_t IpcInterprocessIndexerCommandManager::indexerCommandCount()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t len = 0;
	const uint8_t* buf = access.read(&len);

	if (len < 4 || std::memcmp(buf, "\0\0\0\0", 4) == 0)
		return 0;

	auto all = IpcSerializer::deserializeIndexerCommands(buf, len);
	return all.size();
}

inline std::map<std::string, size_t> IpcInterprocessIndexerCommandManager::indexerCommandCountsBySourceGroup()
{
	std::map<std::string, size_t> counts;

	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t len = 0;
	const uint8_t* buf = access.read(&len);

	if (len < 4 || std::memcmp(buf, "\0\0\0\0", 4) == 0)
		return counts;

	for (const auto& command: IpcSerializer::deserializeIndexerCommands(buf, len))
	{
		counts[command->getSourceGroupId()]++;
	}
	return counts;
}
