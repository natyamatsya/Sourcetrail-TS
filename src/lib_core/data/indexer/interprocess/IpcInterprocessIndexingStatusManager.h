#ifndef IPC_INTERPROCESS_INDEXING_STATUS_MANAGER_H
#define IPC_INTERPROCESS_INDEXING_STATUS_MANAGER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <optional>
#include <string>
#include <vector>

#include "FilePath.h"
#include "IpcSharedMemory.h"
#include "ProcessId.h"
#endif

SRCTRL_EXPORT class IpcInterprocessIndexingStatusManager
{
public:
	IpcInterprocessIndexingStatusManager(
		const std::string& instanceUuid, ProcessId processId, bool isOwner);
	~IpcInterprocessIndexingStatusManager();

	void startIndexingSourceFile(const FilePath& filePath);
	void finishIndexingSourceFile();

	void setIndexingInterrupted(bool interrupted);
	bool getIndexingInterrupted();

	void setQueueStopped(bool stopped);
	bool getQueueStopped();

	ProcessId getNextFinishedProcessId();

	std::vector<FilePath> getCurrentlyIndexedSourceFilePaths();
	std::optional<FilePath> getCurrentSourceFilePathForProcess(ProcessId processId);
	std::vector<FilePath> getCurrentSourceFilePaths();
	std::vector<FilePath> getCrashedSourceFilePaths();

private:
	static const char* s_sharedMemoryNamePrefix;

	std::string m_instanceUuid;
	ProcessId m_processId;
	IpcSharedMemory m_shm;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "IpcInterprocessIndexingStatusManager.inl"
#endif

#endif // IPC_INTERPROCESS_INDEXING_STATUS_MANAGER_H
