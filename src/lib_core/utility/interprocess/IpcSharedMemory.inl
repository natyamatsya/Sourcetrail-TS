// Inline implementations for IpcSharedMemory.h. Included at the end of that header (classic) or via
// the srctrl.interprocess wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>

#include "logging.h"
#endif

// ODR-safe home for the process-local live-handle registry and the thoth-ipc grow shims (anonymous
// namespaces are an ODR trap in headers/inls). The function-local statics dedup across classic TUs
// and the module purview by their common mangling, so there is exactly one registry per process.
namespace ipc_shared_memory_detail
{
// Per-process registry of live IpcSharedMemory instances by logical name.
// CREATE_AND_DELETE clears the segment's global storage; doing that underneath
// live in-process handles dangles their mapped memory/mutex views (observed as
// garbage mutex words and lock timeouts) — warn loudly instead of corrupting
// silently. Deliberately leaked: IpcSharedMemory instances held by static
// singletons destruct during static teardown, which must not touch a
// destroyed mutex/map.
inline std::mutex& liveHandleMutex()
{
	static std::mutex* mutex = new std::mutex();
	return *mutex;
}

inline std::map<std::string, size_t>& liveHandleRegistry()
{
	static auto* registry = new std::map<std::string, size_t>();
	return *registry;
}

inline size_t liveHandleCountLocked(const std::string& name)
{
	const auto it = liveHandleRegistry().find(name);
	return it != liveHandleRegistry().end() ? it->second : 0;
}

template <typename ShmHandle>
bool growSharedMemoryHandle(
	ShmHandle& shm,
	const std::string& memoryName,
	const std::size_t newSize)
{
	if constexpr (requires(ShmHandle& handle) { handle.grow(newSize); })
		return shm.grow(newSize);

	shm.release();
	return shm.acquire(memoryName.c_str(), newSize, thoth::shm::create | thoth::shm::open);
}

// Dependent context so this compiles against thoth-ipc revisions with and
// without the capability query (mirrors growSharedMemoryHandle above).
template <typename ShmHandle>
bool canGrowSharedMemoryHandle()
{
	if constexpr (requires { ShmHandle::can_grow(); })
		return ShmHandle::can_grow();
	else
		return true;	// older/foreign backends: the grow path re-creates by name
}
} // namespace ipc_shared_memory_detail

inline const char* IpcSharedMemory::s_memoryNamePrefix = "srctrl_ipc_mem_";
inline const char* IpcSharedMemory::s_mutexNamePrefix = "srctrl_ipc_mtx_";
inline const char* IpcSharedMemory::s_conditionNamePrefix = "srctrl_ipc_cnd_";

inline std::size_t IpcSharedMemory::liveHandleCount(const std::string& name)
{
	std::lock_guard<std::mutex> lock(ipc_shared_memory_detail::liveHandleMutex());
	return ipc_shared_memory_detail::liveHandleCountLocked(name);
}

inline void IpcSharedMemory::deleteSharedMemory(const std::string& name)
{
	std::string memName = s_memoryNamePrefix + name;
	std::string mtxName = s_mutexNamePrefix + name;

	thoth::shm::remove(memName.c_str());
	thoth::sync::mutex::clear_storage(mtxName.c_str());
}

// Names are passed through at full length: thoth-ipc maps any name exceeding the
// OS limit (THOTH_IPC_SHM_NAME_MAX) to "/<prefix>_<16-hex-FNV1a-of-full-name>",
// consistently across acquire/open/remove — so long names stay unique. (An
// earlier 18-char truncation here silently collided names sharing a prefix.)
inline IpcSharedMemory::IpcSharedMemory(const std::string& name, std::size_t size, AccessMode mode)
	: m_name(name), m_size(size), m_mode(mode)
{
	using enum IpcSharedMemory::AccessMode;
	if (mode == CREATE_AND_DELETE)
	{
		std::lock_guard<std::mutex> lock(ipc_shared_memory_detail::liveHandleMutex());
		if (const size_t liveHandles = ipc_shared_memory_detail::liveHandleCountLocked(m_name);
			liveHandles > 0)
		{
			LOG_WARNING(
				"IpcSharedMemory: re-creating segment '" + m_name + "' underneath " +
				std::to_string(liveHandles) +
				" live handle(s) in this process — their memory/mutex views dangle. A segment "
				"must have exactly one CREATE_AND_DELETE owner per process.");
		}
	}

	unsigned shmMode = 0;
	switch (mode)
	{
	case CREATE_AND_DELETE:
		deleteSharedMemory(m_name);
		shmMode = thoth::shm::create | thoth::shm::open;
		break;
	case OPEN_ONLY:
		shmMode = thoth::shm::open;
		break;
	case OPEN_OR_CREATE:
		shmMode = thoth::shm::create | thoth::shm::open;
		break;
	}

	if (!m_shm.acquire(getMemoryName().c_str(), m_size, shmMode))
		throw std::runtime_error("IpcSharedMemory: failed to acquire shared memory: " + getMemoryName());

	if (!m_mutex.open(getMutexName().c_str()))
		throw std::runtime_error("IpcSharedMemory: failed to open mutex: " + getMutexName());

	if (!m_condition.open(getConditionName().c_str()))
		throw std::runtime_error("IpcSharedMemory: failed to open condition: " + getConditionName());

	{
		std::lock_guard<std::mutex> lock(ipc_shared_memory_detail::liveHandleMutex());
		ipc_shared_memory_detail::liveHandleRegistry()[m_name]++;
	}
}

inline IpcSharedMemory::~IpcSharedMemory()
{
	using enum IpcSharedMemory::AccessMode;
	{
		std::lock_guard<std::mutex> lock(ipc_shared_memory_detail::liveHandleMutex());
		const auto it = ipc_shared_memory_detail::liveHandleRegistry().find(m_name);
		if (it != ipc_shared_memory_detail::liveHandleRegistry().end() && --(it->second) == 0)
			ipc_shared_memory_detail::liveHandleRegistry().erase(it);

		if (const size_t remaining = ipc_shared_memory_detail::liveHandleCountLocked(m_name);
			m_mode == CREATE_AND_DELETE && remaining > 0)
		{
			LOG_WARNING(
				"IpcSharedMemory: deleting segment '" + m_name + "' while " +
				std::to_string(remaining) +
				" live handle(s) remain in this process — their memory/mutex views dangle.");
		}
	}
	try
	{
		m_condition.close();
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

inline const std::string& IpcSharedMemory::name() const
{
	return m_name;
}

inline const uint8_t* IpcSharedMemory::peekMappedMemory(std::size_t* outSize) const
{
	if (outSize)
		*outSize = m_size;
	return static_cast<const uint8_t*>(m_shm.get());
}

inline bool IpcSharedMemory::canGrow() noexcept
{
	return ipc_shared_memory_detail::canGrowSharedMemoryHandle<thoth::shm::handle>();
}

inline void IpcSharedMemory::grow(std::size_t newSize)
{
	if (newSize <= m_size)
		return;

	if (!ipc_shared_memory_detail::growSharedMemoryHandle(m_shm, getMemoryName(), newSize))
		throw std::runtime_error("IpcSharedMemory::grow: failed to grow shared memory: " + getMemoryName());

	m_size = newSize;
}

inline std::string IpcSharedMemory::getMemoryName() const
{
	return s_memoryNamePrefix + m_name;
}

inline std::string IpcSharedMemory::getMutexName() const
{
	return s_mutexNamePrefix + m_name;
}

inline std::string IpcSharedMemory::getConditionName() const
{
	return s_conditionNamePrefix + m_name;
}

inline void IpcSharedMemory::notifyAll()
{
	// broadcast() is sequence-based and does not require the mutex to be held.
	m_condition.broadcast(m_mutex);
}

// --- ScopedAccess ---

inline IpcSharedMemory::ScopedAccess::ScopedAccess(IpcSharedMemory* memory)
	: m_memory(memory)
{
	if (m_memory->m_lockBroken.load())
		throw std::runtime_error(
			"IpcSharedMemory::ScopedAccess lock previously failed for " + m_memory->getMutexName());

	const uint32_t lockTimeoutMs = 500;
	const size_t maxLockAttempts = 20;
	size_t lockAttempts = 0;
	while (!m_memory->m_mutex.lock(lockTimeoutMs))
	{
		lockAttempts++;
		if (lockAttempts % 10 == 0)
			LOG_WARNING_STREAM(
				<< "IpcSharedMemory::ScopedAccess waiting for lock on '" << m_memory->getMutexName()
				<< "' (memory='" << m_memory->getMemoryName() << "', attempts=" << lockAttempts
				<< ", timeoutMs=" << lockTimeoutMs << ")");

		if (lockAttempts >= maxLockAttempts)
		{
			m_memory->m_lockBroken.store(true);
			LOG_ERROR_STREAM(
				<< "IpcSharedMemory::ScopedAccess lock timeout on '" << m_memory->getMutexName()
				<< "' (memory='" << m_memory->getMemoryName() << "', attempts=" << lockAttempts
				<< ", timeoutMs=" << lockTimeoutMs << ")");
			throw std::runtime_error(
				"IpcSharedMemory::ScopedAccess lock timeout on " + m_memory->getMutexName());
		}
	}
	m_locked = true;
	m_memory->m_lockBroken.store(false);

	if (lockAttempts > 0)
		LOG_INFO_STREAM(
			<< "IpcSharedMemory::ScopedAccess acquired lock after " << lockAttempts
			<< " retries on '" << m_memory->getMutexName() << "'");
}

inline IpcSharedMemory::ScopedAccess::~ScopedAccess()
{
	if (!m_locked)
		return;

	if (!m_memory->m_mutex.unlock())
		LOG_ERROR_STREAM(
			<< "IpcSharedMemory::ScopedAccess failed to unlock '" << m_memory->getMutexName() << "'");
	m_locked = false;
}

inline bool IpcSharedMemory::ScopedAccess::wait(uint32_t timeoutMs)
{
	if (!m_locked)
		throw std::runtime_error(
			"IpcSharedMemory::ScopedAccess::wait called without holding the lock on " +
			m_memory->getMutexName());

	// Releases the mutex, waits on the condition (or until timeout), reacquires.
	// The lock is held again on return; the caller re-checks its predicate.
	return m_memory->m_condition.wait(m_memory->m_mutex, timeoutMs);
}

inline void* IpcSharedMemory::ScopedAccess::data()
{
	return m_memory->m_shm.get();
}

inline const void* IpcSharedMemory::ScopedAccess::data() const
{
	return m_memory->m_shm.get();
}

inline std::size_t IpcSharedMemory::ScopedAccess::size() const
{
	return m_memory->m_shm.size();
}

inline void IpcSharedMemory::ScopedAccess::write(const uint8_t* buf, std::size_t len)
{
	using enum IpcSharedMemory::AccessMode;
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

inline const uint8_t* IpcSharedMemory::ScopedAccess::read(std::size_t* outLen) const
{
	const void* mem = data();
	if (!mem)
		throw std::runtime_error("IpcSharedMemory::read: no mapped memory");

	if (outLen)
		*outLen = size();

	return static_cast<const uint8_t*>(mem);
}

inline std::string IpcSharedMemory::ScopedAccess::logString() const
{
	return m_memory->getMemoryName() + " - size: " + std::to_string(size());
}
