#include "TaskMergeStorages.h"

#include "StorageProvider.h"
#ifndef SRCTRL_MODULE_BUILD
#include "TimeStamp.h"
#endif
#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.utility;
#endif

TaskMergeStorages::TaskMergeStorages(std::shared_ptr<StorageProvider> storageProvider)
	: m_storageProvider(storageProvider)
{
	m_changeListener = m_storageProvider->addChangeListener(
		[changed = std::weak_ptr<execution::Condition>(m_storagesChanged)]() {
			if (const std::shared_ptr<execution::Condition> condition = changed.lock())
			{
				condition->notify();
			}
		});
}

TaskMergeStorages::~TaskMergeStorages()
{
	m_storageProvider->removeChangeListener(m_changeListener);
}

exec::task<void> TaskMergeStorages::run()
{
	while (true)
	{
		// Waits until at least 3 storages exist (the largest is left for the writer) or
		// the producers are done, holding no thread.
		std::optional<StorageProvider::WaitResult> result;
		co_await m_storagesChanged->until([&]() {
			result = m_storageProvider->countOrDone(3);
			return result.has_value();
		});
		if (*result == StorageProvider::WaitResult::DONE)
		{
			LOG_INFO(
				"storage pre-merge: " + std::to_string(m_mergeCount) + " merges, busy " +
				std::to_string(m_mergeBusyMs) + " ms");
			co_return;	// no more work will arrive
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
			continue;
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
	}
}
