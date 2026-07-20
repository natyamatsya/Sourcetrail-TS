// Inline implementations for IpcSharedMemoryGarbageCollector.h. Included at the end of that header
// (classic) or via the srctrl.interprocess wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstring>

#include "GarbageCollectorSerializer.h"
#include "TimeStamp.h"
#include "logging.h"
#endif

inline const std::string IpcSharedMemoryGarbageCollector::s_memoryName = "gc_ipc";
inline const size_t IpcSharedMemoryGarbageCollector::s_updateIntervalSeconds = 1;
inline const size_t IpcSharedMemoryGarbageCollector::s_deleteThresholdSeconds = 10;

inline std::shared_ptr<IpcSharedMemoryGarbageCollector> IpcSharedMemoryGarbageCollector::s_instance;

inline IpcSharedMemoryGarbageCollector* IpcSharedMemoryGarbageCollector::createInstance()
{
	using enum IpcSharedMemory::AccessMode;
	try
	{
		if (!s_instance)
			s_instance = std::shared_ptr<IpcSharedMemoryGarbageCollector>(
				new IpcSharedMemoryGarbageCollector());
	}
	catch (std::exception& e)
	{
		LOG_ERROR_STREAM(<< "exception at IPC garbage collector: " << e.what());
	}

	return s_instance.get();
}

inline IpcSharedMemoryGarbageCollector* IpcSharedMemoryGarbageCollector::getInstance()
{
	return s_instance.get();
}

inline void IpcSharedMemoryGarbageCollector::destroyInstance()
{
	s_instance.reset();
}

inline IpcSharedMemoryGarbageCollector::IpcSharedMemoryGarbageCollector()
	: m_shm{s_memoryName, 65536, IpcSharedMemory::AccessMode::OPEN_OR_CREATE}
	, m_loopIsRunning{false}
{
}

inline IpcSharedMemoryGarbageCollector::~IpcSharedMemoryGarbageCollector()
{
	// Signal the thread to stop and join it without touching any IPC resources.
	// IPC resources (m_shm, m_mutex) may already be invalid during static
	// destruction — stop() handles the full cleanup when called explicitly.
	m_loopIsRunning = false;
	m_loopCondition.notify_all();
	if (m_thread && m_thread->joinable())
		m_thread->join();
}

inline void IpcSharedMemoryGarbageCollector::run(const std::string& uuid)
{
	using enum IpcSharedMemory::AccessMode;
	LOG_INFO_STREAM(<< "start IPC shared memory garbage collection");

	m_uuid = uuid;

	m_thread = std::make_shared<std::thread>([this]() {
		m_loopIsRunning = true;

		while (m_loopIsRunning)
		{
			update();
			// Interruptible sleep: stop() notifies the condition so shutdown returns
			// immediately instead of blocking up to s_updateIntervalSeconds on join.
			std::unique_lock<std::mutex> lock(m_loopMutex);
			m_loopCondition.wait_for(
				lock,
				std::chrono::seconds(s_updateIntervalSeconds),
				[this]() { return !m_loopIsRunning.load(); });
		}
	});
}

inline void IpcSharedMemoryGarbageCollector::stop()
{
	using enum IpcSharedMemory::AccessMode;
	LOG_INFO_STREAM(<< "stop IPC shared memory garbage collection");

	m_loopIsRunning = false;
	m_loopCondition.notify_all();

	if (m_thread && m_thread->joinable())
	{
		m_thread->join();
		m_thread.reset();
	}

	{
		std::lock_guard<std::mutex> lock(m_sharedMemoryNamesMutex);
		m_removedSharedMemoryNames.insert(m_sharedMemoryNames.begin(), m_sharedMemoryNames.end());
	}

	update();

	m_sharedMemoryNames.clear();
	m_removedSharedMemoryNames.clear();

	// Remove ourselves from the instance list
	{
		IpcSharedMemory::ScopedAccess access(&m_shm);
		std::size_t len = 0;
		const uint8_t* buf = access.read(&len);

		IpcSerializer::GarbageCollectorData data;
		if (len >= 4 && std::memcmp(buf, "\0\0\0\0", 4) != 0)
			data = IpcSerializer::deserializeGarbageCollector(buf, len);

		// Remove our uuid from instances
		std::erase_if(data.instances, [&](const auto& p) { return p.first == m_uuid; });

		// Check if other instances are still alive
		bool otherRunning = false;
		TimeStamp now = TimeStamp::now();
		for (const auto& [uuid, ts] : data.instances)
		{
			if (now.deltaS(TimeStamp(ts)) <= s_deleteThresholdSeconds)
			{
				otherRunning = true;
				break;
			}
		}

		auto fbBuf = IpcSerializer::serializeGarbageCollector(data);
		access.write(fbBuf.data(), fbBuf.size());

		if (!otherRunning)
		{
			LOG_INFO_STREAM(<< "delete IPC garbage collector memory: " << s_memoryName);
			// Memory will be cleaned up by the IpcSharedMemory destructor
		}
	}
}

inline void IpcSharedMemoryGarbageCollector::registerSharedMemory(const std::string& sharedMemoryName)
{
	{
		std::lock_guard<std::mutex> lock(m_sharedMemoryNamesMutex);
		m_sharedMemoryNames.insert(sharedMemoryName);
	}
	update();
}

inline void IpcSharedMemoryGarbageCollector::unregisterSharedMemory(const std::string& sharedMemoryName)
{
	{
		std::lock_guard<std::mutex> lock(m_sharedMemoryNamesMutex);
		if (m_sharedMemoryNames.erase(sharedMemoryName) > 0)
			m_removedSharedMemoryNames.insert(sharedMemoryName);
	}
	update();
}

inline void IpcSharedMemoryGarbageCollector::update()
{
	using enum IpcSharedMemory::AccessMode;
	std::lock_guard<std::mutex> lock(m_sharedMemoryNamesMutex);

	IpcSharedMemory::ScopedAccess access(&m_shm);
	std::size_t len = 0;
	const uint8_t* buf = access.read(&len);

	IpcSerializer::GarbageCollectorData data;
	if (len >= 4 && std::memcmp(buf, "\0\0\0\0", 4) != 0)
		data = IpcSerializer::deserializeGarbageCollector(buf, len);

	std::string now = TimeStamp::now().toString();

	// Update our instance timestamp
	bool found = false;
	for (auto& [uuid, ts] : data.instances)
	{
		if (uuid == m_uuid)
		{
			ts = now;
			found = true;
			break;
		}
	}
	if (!found)
		data.instances.emplace_back(m_uuid, now);

	// Remove unregistered shared memories
	for (const auto& name : m_removedSharedMemoryNames)
		std::erase_if(data.memoryTimestamps, [&](const auto& p) { return p.first == name; });
	m_removedSharedMemoryNames.clear();

	// Add or update registered shared memories
	for (const auto& name : m_sharedMemoryNames)
	{
		bool updated = false;
		for (auto& [n, ts] : data.memoryTimestamps)
		{
			if (n == name)
			{
				ts = now;
				updated = true;
				break;
			}
		}
		if (!updated)
			data.memoryTimestamps.emplace_back(name, now);
	}

	// Delete stale shared memories
	TimeStamp nowTs = TimeStamp::now();
	std::erase_if(data.memoryTimestamps, [&](const auto& p) {
		TimeStamp ts(p.second);
		if (nowTs.deltaS(ts) > s_deleteThresholdSeconds)
		{
			LOG_INFO_STREAM(<< "IPC collect garbage: " << p.first);
			IpcSharedMemory::deleteSharedMemory(p.first);
			return true;
		}
		return false;
	});

	auto fbBuf = IpcSerializer::serializeGarbageCollector(data);
	access.write(fbBuf.data(), fbBuf.size());
}
