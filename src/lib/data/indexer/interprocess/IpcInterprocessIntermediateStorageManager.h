#ifndef IPC_INTERPROCESS_INTERMEDIATE_STORAGE_MANAGER_H
#define IPC_INTERPROCESS_INTERMEDIATE_STORAGE_MANAGER_H

#include <memory>
#include <string>

#include "IpcSharedMemory.h"
#include "ProcessId.h"

class IntermediateStorage;

class IpcInterprocessIntermediateStorageManager
{
public:
	IpcInterprocessIntermediateStorageManager(
		const std::string& instanceUuid, ProcessId processId, bool isOwner);
	~IpcInterprocessIntermediateStorageManager() = default;

	ProcessId getProcessId() const { return m_processId; }

	void pushIntermediateStorage(const std::shared_ptr<IntermediateStorage>& intermediateStorage);
	std::shared_ptr<IntermediateStorage> popIntermediateStorage();

	size_t getIntermediateStorageCount();

private:
	static const char* s_sharedMemoryNamePrefix;

	std::string m_instanceUuid;
	ProcessId m_processId;
	IpcSharedMemory m_shm;
};

#endif // IPC_INTERPROCESS_INTERMEDIATE_STORAGE_MANAGER_H
