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
