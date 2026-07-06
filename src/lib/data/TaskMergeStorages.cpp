#include "TaskMergeStorages.h"

#include "StorageProvider.h"
#include "TimeStamp.h"
#include "logging.h"

TaskMergeStorages::TaskMergeStorages(std::shared_ptr<StorageProvider> storageProvider)
	: m_storageProvider(storageProvider)
{
}

void TaskMergeStorages::doEnter(std::shared_ptr<Blackboard>  /*blackboard*/) {}

Task::TaskState TaskMergeStorages::doUpdate(std::shared_ptr<Blackboard>  /*blackboard*/)
{
	using enum Task::TaskState;

	// Block until at least 3 storages exist (the largest is left for the writer)
	// or the producers are done. Replaces the old 250 ms poll.
	if (m_storageProvider->waitForCountOrDone(3) == StorageProvider::WaitResult::DONE)
	{
		if (!m_summaryLogged)
		{
			m_summaryLogged = true;
			LOG_INFO(
				"storage pre-merge: " + std::to_string(m_mergeCount) + " merges, busy " +
				std::to_string(m_mergeBusyMs) + " ms");
		}
		return STATE_FAILURE;	 // no more work will arrive -> ends the repeat loop
	}

	std::shared_ptr<IntermediateStorage> target = m_storageProvider->consumeSecondLargestStorage();
	std::shared_ptr<IntermediateStorage> source = m_storageProvider->consumeSecondLargestStorage();
	if (target && source)
	{
		const TimeStamp start = TimeStamp::now();
		target->inject(source.get());
		m_mergeBusyMs += static_cast<long long>(TimeStamp::now().deltaMS(start));
		m_mergeCount++;

		m_storageProvider->insert(target);
		return STATE_SUCCESS;
	}

	// Lost the race against the writer -- put back what we got and retry.
	if (target)
	{
		m_storageProvider->insert(target);
	}
	if (source)
	{
		m_storageProvider->insert(source);
	}
	return STATE_SUCCESS;
}

void TaskMergeStorages::doExit(std::shared_ptr<Blackboard>  /*blackboard*/) {}

void TaskMergeStorages::doReset(std::shared_ptr<Blackboard>  /*blackboard*/) {}

void TaskMergeStorages::terminate()
{
	// Release a blocked doUpdate so TaskGroupParallel::doTerminate's join returns.
	m_storageProvider->setDone();
}
