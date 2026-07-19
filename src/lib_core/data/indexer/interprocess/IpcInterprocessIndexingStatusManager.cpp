#include "IpcInterprocessIndexingStatusManager.h"

#include <algorithm>
#include <cstring>

#include "IndexingStatusSerializer.h"
#include "logging.h"

const char* IpcInterprocessIndexingStatusManager::s_sharedMemoryNamePrefix = "ists_ipc_";

IpcInterprocessIndexingStatusManager::IpcInterprocessIndexingStatusManager(
	const std::string& instanceUuid, ProcessId processId, bool isOwner)
	: m_instanceUuid{instanceUuid}
	, m_processId{processId}
	, m_shm{
		  s_sharedMemoryNamePrefix + instanceUuid,
		  1048576,
		  isOwner ? IpcSharedMemory::AccessMode::CREATE_AND_DELETE : IpcSharedMemory::AccessMode::OPEN_OR_CREATE}
{
}

IpcInterprocessIndexingStatusManager::~IpcInterprocessIndexingStatusManager() = default;

static IpcSerializer::IndexingStatusData readStatus(IpcSharedMemory::ScopedAccess& access)
{
	std::size_t len = 0;
	const uint8_t* buf = access.read(&len);

	if (len >= 4 && std::memcmp(buf, "\0\0\0\0", 4) != 0)
		return IpcSerializer::deserializeIndexingStatus(buf, len);

	return {};
}

static void writeStatus(IpcSharedMemory::ScopedAccess& access, const IpcSerializer::IndexingStatusData& data)
{
	auto fbBuf = IpcSerializer::serializeIndexingStatus(data);
	access.write(fbBuf.data(), fbBuf.size());
}

void IpcInterprocessIndexingStatusManager::startIndexingSourceFile(const FilePath& filePath)
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = readStatus(access);

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

	writeStatus(access, data);
}

void IpcInterprocessIndexingStatusManager::finishIndexingSourceFile()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = readStatus(access);

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

	writeStatus(access, data);
}

void IpcInterprocessIndexingStatusManager::setIndexingInterrupted(bool interrupted)
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = readStatus(access);
	data.indexingInterrupted = interrupted;
	writeStatus(access, data);
}

bool IpcInterprocessIndexingStatusManager::getIndexingInterrupted()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	return readStatus(access).indexingInterrupted;
}

void IpcInterprocessIndexingStatusManager::setQueueStopped(bool stopped)
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = readStatus(access);
	data.queueStopped = stopped;
	writeStatus(access, data);
}

bool IpcInterprocessIndexingStatusManager::getQueueStopped()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	return readStatus(access).queueStopped;
}

ProcessId IpcInterprocessIndexingStatusManager::getNextFinishedProcessId()
{
	using enum IpcSharedMemory::AccessMode;
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = readStatus(access);

	if (data.finishedProcessIds.empty())
		return ProcessId::NONE;

	auto pid = data.finishedProcessIds.front();
	data.finishedProcessIds.erase(data.finishedProcessIds.begin());

	writeStatus(access, data);
	return static_cast<ProcessId>(pid);
}

std::vector<FilePath> IpcInterprocessIndexingStatusManager::getCurrentlyIndexedSourceFilePaths()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = readStatus(access);

	// Drain the indexing files queue (same as boost backend)
	std::vector<FilePath> result;
	result.reserve(data.indexingFilePaths.size());
	for (const auto& p : data.indexingFilePaths)
		result.emplace_back(p);

	data.indexingFilePaths.clear();
	writeStatus(access, data);

	return result;
}

std::optional<FilePath> IpcInterprocessIndexingStatusManager::getCurrentSourceFilePathForProcess(
	const ProcessId processId)
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	const auto data = readStatus(access);

	const auto pid = static_cast<std::size_t>(processId);
	for (const auto& [currentPid, path]: data.currentFiles)
	{
		if (currentPid == pid)
			return FilePath(path);
	}

	return std::nullopt;
}

std::vector<FilePath> IpcInterprocessIndexingStatusManager::getCurrentSourceFilePaths()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	const auto data = readStatus(access);

	std::vector<FilePath> result;
	result.reserve(data.currentFiles.size());
	for (const auto& [pid, path]: data.currentFiles)
		result.emplace_back(path);

	return result;
}

std::vector<FilePath> IpcInterprocessIndexingStatusManager::getCrashedSourceFilePaths()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	auto data = readStatus(access);

	std::vector<FilePath> result;

	// Crashed files
	for (const auto& p : data.crashedFilePaths)
		result.emplace_back(p);

	// Any remaining current files are also considered crashed
	for (const auto& [pid, path] : data.currentFiles)
		result.emplace_back(path);

	return result;
}
