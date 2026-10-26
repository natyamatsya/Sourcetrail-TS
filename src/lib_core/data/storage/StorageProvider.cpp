#include "StorageProvider.h"

#include <vector>

#include "logging.h"

int StorageProvider::getStorageCount() const
{
	std::lock_guard<std::mutex> lock(m_storagesMutex);
	return static_cast<int>(m_storages.size());
}

void StorageProvider::clear()
{
	{
		std::lock_guard<std::mutex> lock(m_storagesMutex);
		m_storages.clear();
	}
	// Wake waiters so they re-check; on interrupt setDone() follows shortly after.
	m_storagesChanged.notify_all();
	changed();
}

void StorageProvider::insert(std::shared_ptr<IntermediateStorage> storage)
{
	const std::size_t storageSize = storage->getSourceLocationCount();
	std::list<std::shared_ptr<IntermediateStorage>>::iterator it;

	{
		std::lock_guard<std::mutex> lock(m_storagesMutex);
		for (it = m_storages.begin(); it != m_storages.end(); it++)
		{
			if ((*it)->getSourceLocationCount() < storageSize)
			{
				break;
			}
		}
		m_storages.insert(it, storage);
	}
	m_storagesChanged.notify_all();
	changed();
}

std::shared_ptr<IntermediateStorage> StorageProvider::consumeSecondLargestStorage()
{
	std::shared_ptr<IntermediateStorage> ret;
	{
		std::lock_guard<std::mutex> lock(m_storagesMutex);
		if (m_storages.size() > 1)
		{
			std::list<std::shared_ptr<IntermediateStorage>>::iterator it = m_storages.begin();
			it++;
			ret = *it;
			m_storages.erase(it);
		}
	}
	return ret;
}

std::shared_ptr<IntermediateStorage> StorageProvider::consumeLargestStorage()
{
	std::shared_ptr<IntermediateStorage> ret;
	{
		std::lock_guard<std::mutex> lock(m_storagesMutex);
		if (!m_storages.empty())
		{
			ret = m_storages.front();
			m_storages.pop_front();
		}
	}

	return ret;
}

StorageProvider::WaitResult StorageProvider::waitForCountOrDone(int minCount)
{
	std::unique_lock<std::mutex> lock(m_storagesMutex);
	m_storagesChanged.wait(
		lock,
		[this, minCount]()
		{ return m_done || static_cast<int>(m_storages.size()) >= minCount; });

	return static_cast<int>(m_storages.size()) >= minCount ? WaitResult::READY : WaitResult::DONE;
}

void StorageProvider::setDone()
{
	{
		std::lock_guard<std::mutex> lock(m_storagesMutex);
		m_done = true;
	}
	m_storagesChanged.notify_all();
	changed();
}

std::optional<StorageProvider::WaitResult> StorageProvider::countOrDone(int minCount) const
{
	std::lock_guard<std::mutex> lock(m_storagesMutex);
	if (static_cast<int>(m_storages.size()) >= minCount)
	{
		return WaitResult::READY;
	}
	return m_done ? std::optional(WaitResult::DONE) : std::nullopt;
}

std::size_t StorageProvider::addChangeListener(std::function<void()> changed)
{
	std::lock_guard<std::mutex> lock(m_listenersMutex);
	const std::size_t id = m_nextListener++;
	m_listeners.emplace(id, std::move(changed));
	return id;
}

void StorageProvider::removeChangeListener(std::size_t id)
{
	std::lock_guard<std::mutex> lock(m_listenersMutex);
	m_listeners.erase(id);
}

void StorageProvider::changed()
{
	std::vector<std::function<void()>> listeners;
	{
		std::lock_guard<std::mutex> lock(m_listenersMutex);
		for (const auto& [id, listener]: m_listeners)
		{
			listeners.push_back(listener);
		}
	}
	for (const std::function<void()>& listener: listeners)
	{
		listener();
	}
}

bool StorageProvider::isDone() const
{
	std::lock_guard<std::mutex> lock(m_storagesMutex);
	return m_done;
}

void StorageProvider::logCurrentState() const
{
	std::string logString = "Storages waiting for injection:";
	{
		std::lock_guard<std::mutex> lock(m_storagesMutex);
		for (const std::shared_ptr<IntermediateStorage>& storage: m_storages)
		{
			logString += " " + std::to_string(storage->getSourceLocationCount()) + ";";
		}
	}
	LOG_INFO(logString);
}
