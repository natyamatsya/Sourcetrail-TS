#include "IpcInterprocessIntermediateStorageManager.h"

#include <cstring>

#include "IntermediateStorage.h"
#include "IntermediateStorageSerializer.h"
#include "logging.h"

const char* IpcInterprocessIntermediateStorageManager::s_sharedMemoryNamePrefix = "iist_ipc_";

// Layout in shared memory: [uint32_t count] [uint32_t size0] [bytes0...] [uint32_t size1] [bytes1...]
// This avoids needing FlatBuffer object API for queue management.

static const size_t HEADER_SIZE = sizeof(uint32_t);

static uint32_t readCount(const uint8_t* buf)
{
	uint32_t count = 0;
	std::memcpy(&count, buf, sizeof(count));
	return count;
}

static void writeCount(uint8_t* buf, uint32_t count)
{
	std::memcpy(buf, &count, sizeof(count));
}

IpcInterprocessIntermediateStorageManager::IpcInterprocessIntermediateStorageManager(
	const std::string& instanceUuid, ProcessId processId, bool isOwner)
	: m_instanceUuid{instanceUuid}
	, m_processId{processId}
	, m_shm{
		  s_sharedMemoryNamePrefix + to_string(processId) + "_" + instanceUuid,
		  3 * 1048576,
		  isOwner ? IpcSharedMemory::CREATE_AND_DELETE : IpcSharedMemory::OPEN_OR_CREATE}
{
}

void IpcInterprocessIntermediateStorageManager::pushIntermediateStorage(
	const std::shared_ptr<IntermediateStorage>& intermediateStorage)
{
	auto fbBuf = IpcSerializer::serializeIntermediateStorage(*intermediateStorage);

	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t shmLen = 0;
	const uint8_t* shmBuf = access.read(&shmLen);

	// Read current count
	uint32_t count = 0;
	if (shmLen >= HEADER_SIZE)
		count = readCount(shmBuf);

	// Calculate total existing payload size
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

	size_t totalNeeded = HEADER_SIZE + existingPayload + sizeof(uint32_t) + fbBuf.size();
	if (totalNeeded > shmLen)
		throw std::runtime_error("IpcIntermediateStorageManager: shared memory too small");

	// Build new buffer: copy existing, then append [size][data]
	std::vector<uint8_t> newBuf(totalNeeded);
	writeCount(newBuf.data(), count + 1);

	if (existingPayload > 0)
		std::memcpy(newBuf.data() + HEADER_SIZE, shmBuf + HEADER_SIZE, existingPayload);

	uint32_t entrySize = static_cast<uint32_t>(fbBuf.size());
	size_t offset = HEADER_SIZE + existingPayload;
	std::memcpy(newBuf.data() + offset, &entrySize, sizeof(entrySize));
	std::memcpy(newBuf.data() + offset + sizeof(entrySize), fbBuf.data(), fbBuf.size());

	access.write(newBuf.data(), newBuf.size());

	LOG_INFO(access.logString());
}

std::shared_ptr<IntermediateStorage> IpcInterprocessIntermediateStorageManager::popIntermediateStorage()
{
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

	// Remove first entry: shift remaining data forward
	size_t firstTotal = sizeof(uint32_t) + firstSize;
	size_t remainStart = HEADER_SIZE + firstTotal;
	size_t remainLen = 0;

	// Calculate remaining payload
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

	std::vector<uint8_t> newBuf(HEADER_SIZE + remainLen);
	writeCount(newBuf.data(), count - 1);
	if (remainLen > 0)
		std::memcpy(newBuf.data() + HEADER_SIZE, shmBuf + remainStart, remainLen);

	access.write(newBuf.data(), newBuf.size());

	LOG_INFO(access.logString());
	return result;
}

size_t IpcInterprocessIntermediateStorageManager::getIntermediateStorageCount()
{
	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t shmLen = 0;
	const uint8_t* shmBuf = access.read(&shmLen);

	if (shmLen < HEADER_SIZE)
		return 0;

	return readCount(shmBuf);
}
