// Inline implementations for IpcInterprocessIndexingStatusManager.h. Included at the end of that
// header (classic) or via the srctrl.interprocess wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <algorithm>
#include <cstring>

#include "IndexingStatusSerializer.h"
#include "logging.h"
#endif

// ODR-safe home for the shared read/write helpers (file-level statics are an ODR trap in
// headers/inls).
namespace ipc_indexing_status_manager_detail
{

inline IpcSerializer::IndexingStatusData readStatus(IpcSharedMemory::ScopedAccess& access)
{
	std::size_t len = 0;
	const uint8_t* buf = access.read(&len);

	if (len >= 4 && std::memcmp(buf, "\0\0\0\0", 4) != 0)
		return IpcSerializer::deserializeIndexingStatus(buf, len);

	return {};
}

inline void writeStatus(IpcSharedMemory::ScopedAccess& access, const IpcSerializer::IndexingStatusData& data)
{
	auto fbBuf = IpcSerializer::serializeIndexingStatus(data);
	access.write(fbBuf.data(), fbBuf.size());
}

}	 // namespace ipc_indexing_status_manager_detail

inline const char* IpcInterprocessIndexingStatusManager::s_sharedMemoryNamePrefix = "ists_ipc_";

inline IpcInterprocessIndexingStatusManager::IpcInterprocessIndexingStatusManager(
	const std::string& instanceUuid, ProcessId processId, bool isOwner)
	: m_instanceUuid{instanceUuid}
	, m_processId{processId}
	, m_shm{
		  s_sharedMemoryNamePrefix + instanceUuid,
		  1048576,
		  isOwner ? IpcSharedMemory::AccessMode::CREATE_AND_DELETE : IpcSharedMemory::AccessMode::OPEN_OR_CREATE}
{
}

inline IpcInterprocessIndexingStatusManager::~IpcInterprocessIndexingStatusManager() = default;

inline void IpcInterprocessIndexingStatusManager::startIndexingSourceFile(const FilePath& filePath)
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = ipc_indexing_status_manager_detail::readStatus(access);

	// Add to indexing queue
	data.indexingFilePaths.push_back(filePath.str());

	// Check if this process already has a current file -> that file crashed
	auto pid = static_cast<std::size_t>(m_processId);
	for (auto it = data.currentFiles.begin(); it != data.currentFiles.end(); ++it)
	{
		if (it->first == pid)
		{
			if (
				std::find(data.crashedFilePaths.begin(), data.crashedFilePaths.end(), it->second) ==
				data.crashedFilePaths.end())
				data.crashedFilePaths.push_back(it->second);
			data.currentFiles.erase(it);
			break;
		}
	}

	// Set current file for this process
	data.currentFiles.emplace_back(pid, filePath.str());

	ipc_indexing_status_manager_detail::writeStatus(access, data);
}

inline void IpcInterprocessIndexingStatusManager::finishIndexingSourceFile()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = ipc_indexing_status_manager_detail::readStatus(access);

	auto pid = static_cast<std::size_t>(m_processId);
	std::string finishedFilePath;

	// Remove from current files
	for (auto it = data.currentFiles.begin(); it != data.currentFiles.end(); ++it)
	{
		if (it->first == pid)
		{
			finishedFilePath = it->second;
			data.currentFiles.erase(it);
			break;
		}
	}

	// Add to finished process IDs
	data.finishedProcessIds.push_back(pid);

	// If a file eventually finishes indexing successfully, clear previous crash marks for it.
	if (!finishedFilePath.empty())
	{
		data.crashedFilePaths.erase(
			std::remove(
				data.crashedFilePaths.begin(), data.crashedFilePaths.end(), finishedFilePath),
			data.crashedFilePaths.end());
	}

	ipc_indexing_status_manager_detail::writeStatus(access, data);
}

inline void IpcInterprocessIndexingStatusManager::setIndexingInterrupted(bool interrupted)
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = ipc_indexing_status_manager_detail::readStatus(access);
	data.indexingInterrupted = interrupted;
	ipc_indexing_status_manager_detail::writeStatus(access, data);
}

inline bool IpcInterprocessIndexingStatusManager::getIndexingInterrupted()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	return ipc_indexing_status_manager_detail::readStatus(access).indexingInterrupted;
}

inline void IpcInterprocessIndexingStatusManager::setQueueStopped(bool stopped)
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = ipc_indexing_status_manager_detail::readStatus(access);
	data.queueStopped = stopped;
	ipc_indexing_status_manager_detail::writeStatus(access, data);
}

inline bool IpcInterprocessIndexingStatusManager::getQueueStopped()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	return ipc_indexing_status_manager_detail::readStatus(access).queueStopped;
}

inline ProcessId IpcInterprocessIndexingStatusManager::getNextFinishedProcessId()
{
	using enum IpcSharedMemory::AccessMode;
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = ipc_indexing_status_manager_detail::readStatus(access);

	if (data.finishedProcessIds.empty())
		return ProcessId::NONE;

	auto pid = data.finishedProcessIds.front();
	data.finishedProcessIds.erase(data.finishedProcessIds.begin());

	ipc_indexing_status_manager_detail::writeStatus(access, data);
	return static_cast<ProcessId>(pid);
}

inline std::vector<FilePath> IpcInterprocessIndexingStatusManager::getCurrentlyIndexedSourceFilePaths()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = ipc_indexing_status_manager_detail::readStatus(access);

	// Drain the indexing files queue (same as boost backend)
	std::vector<FilePath> result;
	result.reserve(data.indexingFilePaths.size());
	for (const auto& p : data.indexingFilePaths)
		result.emplace_back(p);

	data.indexingFilePaths.clear();
	ipc_indexing_status_manager_detail::writeStatus(access, data);

	return result;
}

inline std::optional<FilePath> IpcInterprocessIndexingStatusManager::getCurrentSourceFilePathForProcess(
	const ProcessId processId)
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	const auto data = ipc_indexing_status_manager_detail::readStatus(access);

	const auto pid = static_cast<std::size_t>(processId);
	for (const auto& [currentPid, path]: data.currentFiles)
	{
		if (currentPid == pid)
			return FilePath(path);
	}

	return std::nullopt;
}

inline std::vector<FilePath> IpcInterprocessIndexingStatusManager::getCurrentSourceFilePaths()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	const auto data = ipc_indexing_status_manager_detail::readStatus(access);

	std::vector<FilePath> result;
	result.reserve(data.currentFiles.size());
	for (const auto& [pid, path]: data.currentFiles)
		result.emplace_back(path);

	return result;
}

inline std::vector<FilePath> IpcInterprocessIndexingStatusManager::getCrashedSourceFilePaths()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = ipc_indexing_status_manager_detail::readStatus(access);

	std::vector<FilePath> result;

	// Crashed files
	for (const auto& p : data.crashedFilePaths)
		result.emplace_back(p);

	// Any remaining current files are also considered crashed
	for (const auto& [pid, path] : data.currentFiles)
		result.emplace_back(path);

	return result;
}
