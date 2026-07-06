#ifndef TASK_MERGE_STORAGES_H
#define TASK_MERGE_STORAGES_H

#include <cstddef>
#include <memory>

#include "Task.h"

class StorageProvider;

//! In-memory pre-merge: while indexers run, repeatedly merges the 2nd- and
//! 3rd-largest intermediate storages so the (serial) storage writer sees fewer,
//! bigger, pre-deduplicated storages. Event-driven: blocks on the provider until
//! enough storages exist or producers are done -- no polling.
class TaskMergeStorages: public Task
{
public:
	TaskMergeStorages(std::shared_ptr<StorageProvider> storageProvider);

private:
	void doEnter(std::shared_ptr<Blackboard> blackboard) override;
	TaskState doUpdate(std::shared_ptr<Blackboard> blackboard) override;
	void doExit(std::shared_ptr<Blackboard> blackboard) override;
	void doReset(std::shared_ptr<Blackboard> blackboard) override;
	void terminate() override;

	std::shared_ptr<StorageProvider> m_storageProvider;

	// instrumentation (accumulated across repeat iterations; summary logged once)
	size_t m_mergeCount = 0;
	long long m_mergeBusyMs = 0;
	bool m_summaryLogged = false;
};

#endif	  // TASK_MERGE_STORAGES_H
