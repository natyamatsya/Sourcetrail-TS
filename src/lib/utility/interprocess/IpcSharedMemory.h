#ifndef IPC_SHARED_MEMORY_H
#define IPC_SHARED_MEMORY_H

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>

#include <libipc/condition.h>
#include <libipc/mutex.h>
#include <libipc/shm.h>

class IpcSharedMemory
{
public:
	enum class AccessMode
	{
		CREATE_AND_DELETE,
		OPEN_ONLY,
		OPEN_OR_CREATE
	};

	static std::string checkName(const std::string& name);
	static void deleteSharedMemory(const std::string& name);

	IpcSharedMemory(const std::string& name, std::size_t size, AccessMode mode);
	~IpcSharedMemory();

	IpcSharedMemory(const IpcSharedMemory&) = delete;
	IpcSharedMemory& operator=(const IpcSharedMemory&) = delete;

	class ScopedAccess
	{
	public:
		ScopedAccess(IpcSharedMemory* memory);
		~ScopedAccess();

		ScopedAccess(const ScopedAccess&) = delete;
		ScopedAccess& operator=(const ScopedAccess&) = delete;

		void* data();
		const void* data() const;
		std::size_t size() const;

		void write(const uint8_t* buf, std::size_t len);
		const uint8_t* read(std::size_t* outLen) const;

		//! Atomically release the lock, wait until another process calls
		//! notifyAll() on this segment (or the timeout elapses), then re-acquire
		//! the lock. Must be called with the access held; on return the lock is
		//! held again. Returns true if woken by a notify, false on timeout.
		//! Callers must re-check their predicate in a loop after waking.
		bool wait(uint32_t timeoutMs);

		std::string logString() const;

	private:
		IpcSharedMemory* m_memory;
		bool m_locked = false;
	};

	const std::string& name() const;

	//! Wake every process currently blocked in ScopedAccess::wait() on this
	//! segment. Safe to call without holding the lock (the underlying condition
	//! is sequence-based, so a notify racing a not-yet-sleeping waiter is not
	//! lost). Call it after the state a waiter is waiting for has been written.
	void notifyAll();

	// Lock-free peek at the raw mapped memory. For approximate reads only (e.g.
	// back-pressure checks). Do NOT use for writes or authoritative reads.
	const uint8_t* peekMappedMemory(std::size_t* outSize) const;

	// Re-create the shm segment at a larger size. The mutex is unaffected.
	// Must NOT be called while a ScopedAccess is held.
	void grow(std::size_t newSize);

private:
	static const char* s_memoryNamePrefix;
	static const char* s_mutexNamePrefix;
	static const char* s_conditionNamePrefix;

	std::string getMemoryName() const;
	std::string getMutexName() const;
	std::string getConditionName() const;

	std::string m_name;
	std::size_t m_size;
	AccessMode m_mode;

	ipc::shm::handle m_shm;
	ipc::sync::mutex m_mutex;
	ipc::sync::condition m_condition;
	std::atomic<bool> m_lockBroken{false};
};

#endif // IPC_SHARED_MEMORY_H
