#ifndef IPC_INTERPROCESS_INTERMEDIATE_STORAGE_MANAGER_H
#define IPC_INTERPROCESS_INTERMEDIATE_STORAGE_MANAGER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>
#include <string>

#include "IpcSharedMemory.h"
#include "ProcessId.h"

class IntermediateStorage;
#endif

SRCTRL_EXPORT class IpcInterprocessIntermediateStorageManager
{
public:
	IpcInterprocessIntermediateStorageManager(
		const std::string& instanceUuid, ProcessId processId, bool isOwner);
	~IpcInterprocessIntermediateStorageManager() = default;

	ProcessId getProcessId() const { return m_processId; }

	void pushIntermediateStorage(const std::shared_ptr<IntermediateStorage>& intermediateStorage);
	std::shared_ptr<IntermediateStorage> popIntermediateStorage();

	size_t getIntermediateStorageCount();

	// Lock-free approximate count — safe to call without holding the mutex.
	// Use only for back-pressure checks, not authoritative reads.
	size_t peekCount() const;

private:
	void growIfNeeded();

	static const char* s_sharedMemoryNamePrefix;

	std::string m_instanceUuid;
	ProcessId m_processId;
	IpcSharedMemory m_shm;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "IpcInterprocessIntermediateStorageManager.inl"
#endif

#endif // IPC_INTERPROCESS_INTERMEDIATE_STORAGE_MANAGER_H
