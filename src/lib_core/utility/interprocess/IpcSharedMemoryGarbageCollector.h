#ifndef IPC_SHARED_MEMORY_GARBAGE_COLLECTOR_H
#define IPC_SHARED_MEMORY_GARBAGE_COLLECTOR_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "IpcSharedMemory.h"
#endif

SRCTRL_EXPORT class IpcSharedMemoryGarbageCollector
{
public:
	static IpcSharedMemoryGarbageCollector* createInstance();
	static IpcSharedMemoryGarbageCollector* getInstance();
	//! Destroy during orderly shutdown (after stop()): the collector owns an
	//! IpcSharedMemory by value, and destroying that at static-teardown time
	//! closes its mutex against thoth-ipc's already-destroyed handle cache.
	static void destroyInstance();

	IpcSharedMemoryGarbageCollector();
	~IpcSharedMemoryGarbageCollector();

	void run(const std::string& uuid);
	void stop();

	void registerSharedMemory(const std::string& sharedMemoryName);
	void unregisterSharedMemory(const std::string& sharedMemoryName);

private:
	void update();

	static const std::string s_memoryName;
	static const size_t s_updateIntervalSeconds;
	static const size_t s_deleteThresholdSeconds;

	static std::shared_ptr<IpcSharedMemoryGarbageCollector> s_instance;

	IpcSharedMemory m_shm;
	std::atomic<bool> m_loopIsRunning;
	std::shared_ptr<std::thread> m_thread;
	// Wakes the collector thread's interval sleep so stop()/shutdown is immediate.
	std::mutex m_loopMutex;
	std::condition_variable m_loopCondition;

	std::string m_uuid;

	std::mutex m_sharedMemoryNamesMutex;
	std::set<std::string> m_sharedMemoryNames;
	std::set<std::string> m_removedSharedMemoryNames;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "IpcSharedMemoryGarbageCollector.inl"
#endif

#endif // IPC_SHARED_MEMORY_GARBAGE_COLLECTOR_H
