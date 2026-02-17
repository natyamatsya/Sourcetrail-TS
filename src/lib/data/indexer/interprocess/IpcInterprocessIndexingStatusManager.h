#ifndef IPC_INTERPROCESS_INDEXING_STATUS_MANAGER_H
#define IPC_INTERPROCESS_INDEXING_STATUS_MANAGER_H

#include <string>
#include <vector>

#include "FilePath.h"
#include "IpcSharedMemory.h"
#include "ProcessId.h"

class IpcInterprocessIndexingStatusManager
{
public:
	IpcInterprocessIndexingStatusManager(
		const std::string& instanceUuid, ProcessId processId, bool isOwner);
	~IpcInterprocessIndexingStatusManager();

	void startIndexingSourceFile(const FilePath& filePath);
	void finishIndexingSourceFile();

	void setIndexingInterrupted(bool interrupted);
	bool getIndexingInterrupted();

	ProcessId getNextFinishedProcessId();

	std::vector<FilePath> getCurrentlyIndexedSourceFilePaths();
	std::vector<FilePath> getCrashedSourceFilePaths();

private:
	static const char* s_sharedMemoryNamePrefix;

	std::string m_instanceUuid;
	ProcessId m_processId;
	IpcSharedMemory m_shm;
};

#endif // IPC_INTERPROCESS_INDEXING_STATUS_MANAGER_H
