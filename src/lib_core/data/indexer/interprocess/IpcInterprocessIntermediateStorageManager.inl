// Inline implementations for IpcInterprocessIntermediateStorageManager.h. Included at the end of
// that header (classic) or via the srctrl.interprocess wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstring>

#include "IntermediateStorage.h"
#include "IntermediateStorageSerializer.h"
#include "logging.h"
#endif

// Layout in shared memory:
//   [uint64_t needed_capacity] [uint32_t count] [uint32_t size0] [bytes0...] ...
//
// needed_capacity: written by the subprocess before it grows the segment.
//   Zero means "no grow requested". The parent reads this field after acquiring
//   the lock; if it exceeds the current mapped size the parent grows its own
//   handle to match before reading the payload.
//
//   Growth is a Linux-only capability (docs/adr/ADR-0002-no-shm-growth.md):
//   where IpcSharedMemory::canGrow() is false the field is never written to a
//   non-zero value and never acted upon — writers must chunk oversized
//   payloads instead (the Rust indexer chunks unconditionally and always
//   writes 0). The field stays in the header for wire compatibility.

// ODR-safe home for the header-layout constants and field accessors (file-level statics are an ODR
// trap in headers/inls).
namespace ipc_intermediate_storage_manager_detail
{

inline constexpr size_t CAP_FIELD_SIZE   = sizeof(uint64_t);
inline constexpr size_t COUNT_FIELD_SIZE = sizeof(uint32_t);
inline constexpr size_t HEADER_SIZE      = CAP_FIELD_SIZE + COUNT_FIELD_SIZE;

inline uint64_t readNeededCapacity(const uint8_t* buf)
{
	uint64_t cap = 0;
	std::memcpy(&cap, buf, sizeof(cap));
	return cap;
}

inline void writeNeededCapacity(uint8_t* buf, uint64_t cap)
{
	std::memcpy(buf, &cap, sizeof(cap));
}

inline uint32_t readCount(const uint8_t* buf)
{
	uint32_t count = 0;
	std::memcpy(&count, buf + CAP_FIELD_SIZE, sizeof(count));
	return count;
}

inline void writeCount(uint8_t* buf, uint32_t count)
{
	std::memcpy(buf + CAP_FIELD_SIZE, &count, sizeof(count));
}

}	 // namespace ipc_intermediate_storage_manager_detail

inline const char* IpcInterprocessIntermediateStorageManager::s_sharedMemoryNamePrefix = "iist_ipc_";

inline IpcInterprocessIntermediateStorageManager::IpcInterprocessIntermediateStorageManager(
	const std::string& instanceUuid, ProcessId processId, bool isOwner)
	: m_instanceUuid{instanceUuid}
	, m_processId{processId}
	, m_shm{
		  s_sharedMemoryNamePrefix + to_string(processId) + "_" + instanceUuid,
		  16 * 1048576,
		  isOwner ? IpcSharedMemory::AccessMode::CREATE_AND_DELETE : IpcSharedMemory::AccessMode::OPEN_OR_CREATE}
{
}

inline void IpcInterprocessIntermediateStorageManager::pushIntermediateStorage(
	const std::shared_ptr<IntermediateStorage>& intermediateStorage)
{
	namespace detail = ipc_intermediate_storage_manager_detail;
	using enum IpcSharedMemory::AccessMode;
	auto fbBuf = IpcSerializer::serializeIntermediateStorage(*intermediateStorage);

	// Phase 1: probe existing queue size (under lock), release lock, then grow if needed.
	size_t growTo = 0;
	{
		IpcSharedMemory::ScopedAccess probe(&m_shm);
		std::size_t shmLen = 0;
		const uint8_t* shmBuf = probe.read(&shmLen);

		uint32_t count = (shmLen >= detail::HEADER_SIZE) ? detail::readCount(shmBuf) : 0;
		size_t existingPayload = 0;
		{
			const uint8_t* p = shmBuf + detail::HEADER_SIZE;
			for (uint32_t i = 0; i < count; i++)
			{
				uint32_t entrySize = 0;
				std::memcpy(&entrySize, p, sizeof(entrySize));
				p += sizeof(entrySize) + entrySize;
				existingPayload = static_cast<size_t>(p - shmBuf - detail::HEADER_SIZE);
			}
		}

		const size_t totalNeeded = detail::HEADER_SIZE + existingPayload + sizeof(uint32_t) + fbBuf.size();
		if (totalNeeded > shmLen && IpcSharedMemory::canGrow())
		{
			growTo = totalNeeded * 2;
			// Signal the parent: write needed capacity into the cap field before growing.
			// We can write into the current (small) segment since cap field always fits.
			std::vector<uint8_t> capBuf(shmLen, 0);
			std::memcpy(capBuf.data(), shmBuf, shmLen);
			detail::writeNeededCapacity(capBuf.data(), static_cast<uint64_t>(growTo));
			probe.write(capBuf.data(), capBuf.size());
			LOG_INFO_STREAM(
				<< "IpcIntermediateStorageManager: growing shared memory from " << shmLen
				<< " to " << growTo << " bytes for " << m_shm.name());
		}
		// probe destroyed here — lock released before grow().
	}

	if (growTo > 0)
		m_shm.grow(growTo);

	// Phase 2: write payload under lock.
	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t shmLen = 0;
	const uint8_t* shmBuf = access.read(&shmLen);

	uint32_t count = (shmLen >= detail::HEADER_SIZE) ? detail::readCount(shmBuf) : 0;
	size_t existingPayload = 0;
	{
		const uint8_t* p = shmBuf + detail::HEADER_SIZE;
		for (uint32_t i = 0; i < count; i++)
		{
			uint32_t entrySize = 0;
			std::memcpy(&entrySize, p, sizeof(entrySize));
			p += sizeof(entrySize) + entrySize;
			existingPayload = static_cast<size_t>(p - shmBuf - detail::HEADER_SIZE);
		}
	}

	const size_t needed = detail::HEADER_SIZE + existingPayload + sizeof(uint32_t) + fbBuf.size();
	if (needed > shmLen)
		throw std::runtime_error(
			"IpcIntermediateStorageManager: payload does not fit the shared memory segment "
			"(growth unsupported on this platform or grow attempt failed) — oversized "
			"results must be chunked (docs/adr/ADR-0002-no-shm-growth.md)");

	std::vector<uint8_t> newBuf(shmLen, 0);
	// Preserve the cap signal — only the parent clears it after re-mapping.
	detail::writeNeededCapacity(newBuf.data(), detail::readNeededCapacity(shmBuf));
	detail::writeCount(newBuf.data(), count + 1);

	if (existingPayload > 0)
		std::memcpy(newBuf.data() + detail::HEADER_SIZE, shmBuf + detail::HEADER_SIZE, existingPayload);

	uint32_t entrySize = static_cast<uint32_t>(fbBuf.size());
	size_t offset = detail::HEADER_SIZE + existingPayload;
	std::memcpy(newBuf.data() + offset, &entrySize, sizeof(entrySize));
	std::memcpy(newBuf.data() + offset + sizeof(entrySize), fbBuf.data(), fbBuf.size());

	access.write(newBuf.data(), needed);

	LOG_INFO(access.logString());
}

// Check the needed_capacity field lock-free and grow m_shm if it requests more space.
// The subprocess writes needed_capacity before calling grow() (outside the mutex),
// so this field can be read safely without holding the lock.
inline void IpcInterprocessIntermediateStorageManager::growIfNeeded()
{
	namespace detail = ipc_intermediate_storage_manager_detail;
	using enum IpcSharedMemory::AccessMode;
	// Segments are fixed-size where growth is unsupported; subprocesses chunk
	// their payloads there and never request capacity (ADR-0002).
	if (!IpcSharedMemory::canGrow())
		return;
	std::size_t shmLen = 0;
	const uint8_t* shmBuf = m_shm.peekMappedMemory(&shmLen);
	if (!shmBuf || shmLen < detail::CAP_FIELD_SIZE)
		return;
	const uint64_t neededCap = detail::readNeededCapacity(shmBuf);
	if (neededCap <= static_cast<uint64_t>(shmLen))
		return;
	LOG_INFO_STREAM(
		<< "IpcIntermediateStorageManager: parent growing segment from "
		<< shmLen << " to " << neededCap << " bytes for " << m_shm.name());
	m_shm.grow(static_cast<std::size_t>(neededCap));
}

inline std::shared_ptr<IntermediateStorage> IpcInterprocessIntermediateStorageManager::popIntermediateStorage()
{
	namespace detail = ipc_intermediate_storage_manager_detail;
	using enum IpcSharedMemory::AccessMode;
	growIfNeeded();

	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t shmLen = 0;
	const uint8_t* shmBuf = access.read(&shmLen);

	if (shmLen < detail::HEADER_SIZE)
		return nullptr;

	uint32_t count = detail::readCount(shmBuf);
	if (count == 0)
		return nullptr;

	// Read first entry
	const uint8_t* p = shmBuf + detail::HEADER_SIZE;
	uint32_t firstSize = 0;
	std::memcpy(&firstSize, p, sizeof(firstSize));
	const uint8_t* firstData = p + sizeof(firstSize);

	auto result = IpcSerializer::deserializeIntermediateStorage(firstData, firstSize);

	// Remove first entry
	size_t firstTotal = sizeof(uint32_t) + firstSize;
	size_t remainStart = detail::HEADER_SIZE + firstTotal;
	size_t remainLen = 0;
	{
		const uint8_t* q = shmBuf + remainStart;
		for (uint32_t i = 1; i < count; i++)
		{
			uint32_t entrySize = 0;
			std::memcpy(&entrySize, q, sizeof(entrySize));
			q += sizeof(entrySize) + entrySize;
		}
		remainLen = static_cast<size_t>(q - shmBuf - remainStart);
	}

	std::vector<uint8_t> newBuf(detail::HEADER_SIZE + remainLen, 0);
	detail::writeNeededCapacity(newBuf.data(), 0);
	detail::writeCount(newBuf.data(), count - 1);
	if (remainLen > 0)
		std::memcpy(newBuf.data() + detail::HEADER_SIZE, shmBuf + remainStart, remainLen);

	access.write(newBuf.data(), newBuf.size());

	LOG_INFO(access.logString());
	return result;
}

inline size_t IpcInterprocessIntermediateStorageManager::getIntermediateStorageCount()
{
	namespace detail = ipc_intermediate_storage_manager_detail;
	using enum IpcSharedMemory::AccessMode;
	growIfNeeded();

	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t shmLen = 0;
	const uint8_t* shmBuf = access.read(&shmLen);
	if (shmLen < detail::HEADER_SIZE)
		return 0;
	return detail::readCount(shmBuf);
}

inline size_t IpcInterprocessIntermediateStorageManager::peekCount() const
{
	namespace detail = ipc_intermediate_storage_manager_detail;
	using enum IpcSharedMemory::AccessMode;
	std::size_t shmLen = 0;
	const uint8_t* shmBuf = m_shm.peekMappedMemory(&shmLen);
	if (!shmBuf || shmLen < detail::HEADER_SIZE)
		return 0;
	uint32_t count = 0;
	std::memcpy(&count, shmBuf + detail::CAP_FIELD_SIZE, sizeof(count));
	return count;
}
