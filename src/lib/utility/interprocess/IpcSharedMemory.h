#ifndef IPC_SHARED_MEMORY_H
#define IPC_SHARED_MEMORY_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include <libipc/shm.h>
#include <libipc/mutex.h>

class IpcSharedMemory
{
public:
	enum AccessMode
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

		std::string logString() const;

	private:
		IpcSharedMemory* m_memory;
	};

	const std::string& name() const;

	// Lock-free peek at the raw mapped memory. For approximate reads only (e.g.
	// back-pressure checks). Do NOT use for writes or authoritative reads.
	const uint8_t* peekMappedMemory(std::size_t* outSize) const;

	// Re-create the shm segment at a larger size. The mutex is unaffected.
	// Must NOT be called while a ScopedAccess is held.
	void grow(std::size_t newSize);

private:
	static const char* s_memoryNamePrefix;
	static const char* s_mutexNamePrefix;

	std::string getMemoryName() const;
	std::string getMutexName() const;

	std::string m_name;
	std::size_t m_size;
	AccessMode m_mode;

	ipc::shm::handle m_shm;
	ipc::sync::mutex m_mutex;
};

#endif // IPC_SHARED_MEMORY_H
