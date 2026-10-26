#ifndef TASK_MERGE_STORAGES_H
#define TASK_MERGE_STORAGES_H

#include <cstddef>
#include <memory>

#include "Condition.h"
#include "StdexecPrelude.h"

#include <exec/task.hpp>

class StorageProvider;

//! In-memory pre-merge: while indexers run, repeatedly merges the 2nd- and
//! 3rd-largest intermediate storages so the (serial) storage writer sees fewer,
//! bigger, pre-deduplicated storages. Event-driven: it waits, holding no thread, until
//! the provider has enough storages or the producers are done -- no polling.
class TaskMergeStorages
{
public:
	~TaskMergeStorages();
	TaskMergeStorages(std::shared_ptr<StorageProvider> storageProvider);

	//! Merges until the producers are done.
	exec::task<void> run();

private:
	std::shared_ptr<StorageProvider> m_storageProvider;
	// Notified when the provider's storages change.
	std::shared_ptr<execution::Condition> m_storagesChanged = std::make_shared<execution::Condition>();
	std::size_t m_changeListener = 0;

	// instrumentation
	size_t m_mergeCount = 0;
	long long m_mergeBusyMs = 0;
};

#endif	  // TASK_MERGE_STORAGES_H
