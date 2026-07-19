#ifndef STORAGE_PROVIDER_H
#define STORAGE_PROVIDER_H

#include "IntermediateStorage.h"
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>

class StorageProvider
{
public:
	int getStorageCount() const;

	void clear();

	void insert(std::shared_ptr<IntermediateStorage> storage);

	// returns empty shared_ptr if no storages available
	std::shared_ptr<IntermediateStorage> consumeSecondLargestStorage();

	// returns empty shared_ptr if no storages available
	std::shared_ptr<IntermediateStorage> consumeLargestStorage();

	void logCurrentState() const;

	//! Result of waitForCountOrDone().
	enum class WaitResult
	{
		READY,	  //!< the requested storage count is available
		DONE	  //!< producers are done (setDone) and the count cannot be reached
	};

	//! Blocks until getStorageCount() >= minCount (READY), or setDone() was called
	//! while the count is below minCount (DONE). Event-driven replacement for the
	//! fixed-interval polling the merge/inject tasks used to do.
	WaitResult waitForCountOrDone(int minCount);

	//! Marks the producing side finished (or tearing down): releases all waiters.
	//! Idempotent. Consumers drain the remainder and stop once it is empty.
	void setDone();

	bool isDone() const;

private:
	std::list<std::shared_ptr<IntermediateStorage>> m_storages;	   // larger storages are in front
	mutable std::mutex m_storagesMutex;
	std::condition_variable m_storagesChanged;
	bool m_done = false;
};

#endif	  // STORAGE_PROVIDER_H
