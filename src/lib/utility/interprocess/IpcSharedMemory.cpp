#include "IpcSharedMemory.h"

#include <cstring>
#include <stdexcept>

#include "logging.h"

const char* IpcSharedMemory::s_memoryNamePrefix = "srctrl_ipc_mem_";
const char* IpcSharedMemory::s_mutexNamePrefix = "srctrl_ipc_mtx_";

std::string IpcSharedMemory::checkName(const std::string& name)
{
	return name.size() > 18 ? name.substr(0, 18) : name;
}

void IpcSharedMemory::deleteSharedMemory(const std::string& name)
{
	std::string memName = s_memoryNamePrefix + name;
	std::string mtxName = s_mutexNamePrefix + name;

	ipc::shm::remove(memName.c_str());
	ipc::sync::mutex::clear_storage(mtxName.c_str());
}

IpcSharedMemory::IpcSharedMemory(const std::string& name, std::size_t size, AccessMode mode)
	: m_name(checkName(name)), m_size(size), m_mode(mode)
{
	unsigned shmMode = 0;
	switch (mode)
	{
	case CREATE_AND_DELETE:
		deleteSharedMemory(m_name);
		shmMode = ipc::shm::create | ipc::shm::open;
		break;
	case OPEN_ONLY:
		shmMode = ipc::shm::open;
		break;
	case OPEN_OR_CREATE:
		shmMode = ipc::shm::create | ipc::shm::open;
		break;
	}

	if (!m_shm.acquire(getMemoryName().c_str(), m_size, shmMode))
		throw std::runtime_error("IpcSharedMemory: failed to acquire shared memory: " + getMemoryName());

	if (!m_mutex.open(getMutexName().c_str()))
		throw std::runtime_error("IpcSharedMemory: failed to open mutex: " + getMutexName());
}

IpcSharedMemory::~IpcSharedMemory()
{
	try
	{
		m_mutex.close();
		m_shm.release();

		if (m_mode == CREATE_AND_DELETE)
			deleteSharedMemory(m_name);
	}
	catch (...)
	{
		LOG_ERROR("Exception in IpcSharedMemory destructor");
	}
}

const std::string& IpcSharedMemory::name() const
{
	return m_name;
}

const uint8_t* IpcSharedMemory::peekMappedMemory(std::size_t* outSize) const
{
	if (outSize)
		*outSize = m_size;
	return static_cast<const uint8_t*>(m_shm.get());
}

void IpcSharedMemory::grow(std::size_t newSize)
{
	if (newSize <= m_size)
		return;

	m_shm.release();
	// Do NOT unlink the backing file — we extend it in-place via ftruncate
	// so that the other process's handle can re-mmap the same name at the new size.
	m_size = newSize;
	if (!m_shm.acquire(getMemoryName().c_str(), m_size, ipc::shm::create | ipc::shm::open))
		throw std::runtime_error("IpcSharedMemory::grow: failed to re-map shared memory: " + getMemoryName());
}

std::string IpcSharedMemory::getMemoryName() const
{
	return s_memoryNamePrefix + m_name;
}

std::string IpcSharedMemory::getMutexName() const
{
	return s_mutexNamePrefix + m_name;
}

// --- ScopedAccess ---

IpcSharedMemory::ScopedAccess::ScopedAccess(IpcSharedMemory* memory)
	: m_memory(memory)
{
	const uint32_t lockTimeoutMs = 500;
	size_t lockAttempts = 0;
	while (!m_memory->m_mutex.lock(lockTimeoutMs))
	{
		lockAttempts++;
		if (lockAttempts % 10 == 0)
			LOG_WARNING_STREAM(
				<< "IpcSharedMemory::ScopedAccess waiting for lock on '" << m_memory->getMutexName()
				<< "' (memory='" << m_memory->getMemoryName() << "', attempts=" << lockAttempts
				<< ", timeoutMs=" << lockTimeoutMs << ")");
	}

	if (lockAttempts > 0)
		LOG_INFO_STREAM(
			<< "IpcSharedMemory::ScopedAccess acquired lock after " << lockAttempts
			<< " retries on '" << m_memory->getMutexName() << "'");
}

IpcSharedMemory::ScopedAccess::~ScopedAccess()
{
	m_memory->m_mutex.unlock();
}

void* IpcSharedMemory::ScopedAccess::data()
{
	return m_memory->m_shm.get();
}

const void* IpcSharedMemory::ScopedAccess::data() const
{
	return m_memory->m_shm.get();
}

std::size_t IpcSharedMemory::ScopedAccess::size() const
{
	return m_memory->m_shm.size();
}

void IpcSharedMemory::ScopedAccess::write(const uint8_t* buf, std::size_t len)
{
	void* mem = data();
	if (!mem)
		throw std::runtime_error("IpcSharedMemory::write: no mapped memory");

	if (len > size())
	{
		LOG_ERROR(
			"IpcSharedMemory::write: buffer too large (" + std::to_string(len) +
			" bytes) for shared memory region '" + m_memory->getMemoryName() +
			"' (" + std::to_string(size()) + " bytes)");
		throw std::runtime_error("IpcSharedMemory::write: buffer too large for shared memory region");
	}

	std::memcpy(mem, buf, len);
}

const uint8_t* IpcSharedMemory::ScopedAccess::read(std::size_t* outLen) const
{
	const void* mem = data();
	if (!mem)
		throw std::runtime_error("IpcSharedMemory::read: no mapped memory");

	if (outLen)
		*outLen = size();

	return static_cast<const uint8_t*>(mem);
}

std::string IpcSharedMemory::ScopedAccess::logString() const
{
	return m_memory->getMemoryName() + " - size: " + std::to_string(size());
}
