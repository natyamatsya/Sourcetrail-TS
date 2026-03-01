#include "IpcInterprocessIntermediateStorageManager.h"

#include <cstring>

#include "IntermediateStorage.h"
#include "IntermediateStorageSerializer.h"
#include "logging.h"

const char* IpcInterprocessIntermediateStorageManager::s_sharedMemoryNamePrefix = "iist_ipc_";

// Layout in shared memory:
//   [uint64_t needed_capacity] [uint32_t count] [uint32_t size0] [bytes0...] ...
//
// needed_capacity: written by the subprocess before it grows the segment.
//   Zero means "no grow requested". The parent reads this field after acquiring
//   the lock; if it exceeds the current mapped size the parent grows its own
//   handle to match before reading the payload.

static const size_t CAP_FIELD_SIZE  = sizeof(uint64_t);
static const size_t COUNT_FIELD_SIZE = sizeof(uint32_t);
static const size_t HEADER_SIZE      = CAP_FIELD_SIZE + COUNT_FIELD_SIZE;

static uint64_t readNeededCapacity(const uint8_t* buf)
{
	uint64_t cap = 0;
	std::memcpy(&cap, buf, sizeof(cap));
	return cap;
}

static void writeNeededCapacity(uint8_t* buf, uint64_t cap)
{
	std::memcpy(buf, &cap, sizeof(cap));
}

static uint32_t readCount(const uint8_t* buf)
{
	using enum IpcSharedMemory::AccessMode;
	uint32_t count = 0;
	std::memcpy(&count, buf + CAP_FIELD_SIZE, sizeof(count));
	return count;
}

static void writeCount(uint8_t* buf, uint32_t count)
{
	using enum IpcSharedMemory::AccessMode;
	std::memcpy(buf + CAP_FIELD_SIZE, &count, sizeof(count));
}

IpcInterprocessIntermediateStorageManager::IpcInterprocessIntermediateStorageManager(
	const std::string& instanceUuid, ProcessId processId, bool isOwner)
	: m_instanceUuid{instanceUuid}
	, m_processId{processId}
	, m_shm{
		  s_sharedMemoryNamePrefix + to_string(processId) + "_" + instanceUuid,
		  16 * 1048576,
		  isOwner ? IpcSharedMemory::AccessMode::CREATE_AND_DELETE : IpcSharedMemory::AccessMode::OPEN_OR_CREATE}
{
}

void IpcInterprocessIntermediateStorageManager::pushIntermediateStorage(
	const std::shared_ptr<IntermediateStorage>& intermediateStorage)
{
	using enum IpcSharedMemory::AccessMode;
	auto fbBuf = IpcSerializer::serializeIntermediateStorage(*intermediateStorage);

	// Phase 1: probe existing queue size (under lock), release lock, then grow if needed.
	size_t growTo = 0;
	{
		IpcSharedMemory::ScopedAccess probe(&m_shm);
		std::size_t shmLen = 0;
		const uint8_t* shmBuf = probe.read(&shmLen);

		uint32_t count = (shmLen >= HEADER_SIZE) ? readCount(shmBuf) : 0;
		size_t existingPayload = 0;
		{
			const uint8_t* p = shmBuf + HEADER_SIZE;
			for (uint32_t i = 0; i < count; i++)
			{
				uint32_t entrySize = 0;
				std::memcpy(&entrySize, p, sizeof(entrySize));
				p += sizeof(entrySize) + entrySize;
				existingPayload = static_cast<size_t>(p - shmBuf - HEADER_SIZE);
			}
		}

		const size_t totalNeeded = HEADER_SIZE + existingPayload + sizeof(uint32_t) + fbBuf.size();
		if (totalNeeded > shmLen)
		{
			growTo = totalNeeded * 2;
			// Signal the parent: write needed capacity into the cap field before growing.
			// We can write into the current (small) segment since cap field always fits.
			std::vector<uint8_t> capBuf(shmLen, 0);
			std::memcpy(capBuf.data(), shmBuf, shmLen);
			writeNeededCapacity(capBuf.data(), static_cast<uint64_t>(growTo));
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

	uint32_t count = (shmLen >= HEADER_SIZE) ? readCount(shmBuf) : 0;
	size_t existingPayload = 0;
	{
		const uint8_t* p = shmBuf + HEADER_SIZE;
		for (uint32_t i = 0; i < count; i++)
		{
			uint32_t entrySize = 0;
			std::memcpy(&entrySize, p, sizeof(entrySize));
			p += sizeof(entrySize) + entrySize;
			existingPayload = static_cast<size_t>(p - shmBuf - HEADER_SIZE);
		}
	}

	const size_t needed = HEADER_SIZE + existingPayload + sizeof(uint32_t) + fbBuf.size();
	if (needed > shmLen)
		throw std::runtime_error(
			"IpcIntermediateStorageManager: shared memory too small even after grow attempt");

	std::vector<uint8_t> newBuf(shmLen, 0);
	// Preserve the cap signal — only the parent clears it after re-mapping.
	writeNeededCapacity(newBuf.data(), readNeededCapacity(shmBuf));
	writeCount(newBuf.data(), count + 1);

	if (existingPayload > 0)
		std::memcpy(newBuf.data() + HEADER_SIZE, shmBuf + HEADER_SIZE, existingPayload);

	uint32_t entrySize = static_cast<uint32_t>(fbBuf.size());
	size_t offset = HEADER_SIZE + existingPayload;
	std::memcpy(newBuf.data() + offset, &entrySize, sizeof(entrySize));
	std::memcpy(newBuf.data() + offset + sizeof(entrySize), fbBuf.data(), fbBuf.size());

	access.write(newBuf.data(), needed);

	LOG_INFO(access.logString());
}

// Check the needed_capacity field lock-free and grow m_shm if it requests more space.
// The subprocess writes needed_capacity before calling grow() (outside the mutex),
// so this field can be read safely without holding the lock.
void IpcInterprocessIntermediateStorageManager::growIfNeeded()
{
	using enum IpcSharedMemory::AccessMode;
	std::size_t shmLen = 0;
	const uint8_t* shmBuf = m_shm.peekMappedMemory(&shmLen);
	if (!shmBuf || shmLen < CAP_FIELD_SIZE)
		return;
	const uint64_t neededCap = readNeededCapacity(shmBuf);
	if (neededCap <= static_cast<uint64_t>(shmLen))
		return;
	LOG_INFO_STREAM(
		<< "IpcIntermediateStorageManager: parent growing segment from "
		<< shmLen << " to " << neededCap << " bytes for " << m_shm.name());
	m_shm.grow(static_cast<std::size_t>(neededCap));
}

std::shared_ptr<IntermediateStorage> IpcInterprocessIntermediateStorageManager::popIntermediateStorage()
{
	using enum IpcSharedMemory::AccessMode;
	growIfNeeded();

	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t shmLen = 0;
	const uint8_t* shmBuf = access.read(&shmLen);

	if (shmLen < HEADER_SIZE)
		return nullptr;

	uint32_t count = readCount(shmBuf);
	if (count == 0)
		return nullptr;

	// Read first entry
	const uint8_t* p = shmBuf + HEADER_SIZE;
	uint32_t firstSize = 0;
	std::memcpy(&firstSize, p, sizeof(firstSize));
	const uint8_t* firstData = p + sizeof(firstSize);

	auto result = IpcSerializer::deserializeIntermediateStorage(firstData, firstSize);

	// Remove first entry
	size_t firstTotal = sizeof(uint32_t) + firstSize;
	size_t remainStart = HEADER_SIZE + firstTotal;
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

	std::vector<uint8_t> newBuf(HEADER_SIZE + remainLen, 0);
	writeNeededCapacity(newBuf.data(), 0);
	writeCount(newBuf.data(), count - 1);
	if (remainLen > 0)
		std::memcpy(newBuf.data() + HEADER_SIZE, shmBuf + remainStart, remainLen);

	access.write(newBuf.data(), newBuf.size());

	LOG_INFO(access.logString());
	return result;
}

size_t IpcInterprocessIntermediateStorageManager::getIntermediateStorageCount()
{
	using enum IpcSharedMemory::AccessMode;
	growIfNeeded();

	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t shmLen = 0;
	const uint8_t* shmBuf = access.read(&shmLen);
	if (shmLen < HEADER_SIZE)
		return 0;
	return readCount(shmBuf);
}

size_t IpcInterprocessIntermediateStorageManager::peekCount() const
{
	using enum IpcSharedMemory::AccessMode;
	std::size_t shmLen = 0;
	const uint8_t* shmBuf = m_shm.peekMappedMemory(&shmLen);
	if (!shmBuf || shmLen < HEADER_SIZE)
		return 0;
	uint32_t count = 0;
	std::memcpy(&count, shmBuf + CAP_FIELD_SIZE, sizeof(count));
	return count;
}
