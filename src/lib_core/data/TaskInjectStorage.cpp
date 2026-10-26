#include "TaskInjectStorage.h"

#ifndef SRCTRL_MODULE_BUILD
#include "IntermediateStorage.h"
#include "Storage.h"
#endif
#include "StorageProvider.h"
#ifndef SRCTRL_MODULE_BUILD
#include "TimeStamp.h"
#endif
#include "logging.h"

#ifdef SOURCETRAIL_TURSO_CONCURRENT
#include "PersistentStorage.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.storage;
import srctrl.utility;
#endif

TaskInjectStorage::TaskInjectStorage(
	std::shared_ptr<StorageProvider> storageProvider, std::weak_ptr<Storage> target)
	: m_storageProvider(storageProvider), m_target(target)
{
	m_changeListener = m_storageProvider->addChangeListener(
		[changed = std::weak_ptr<execution::Condition>(m_storagesChanged)]() {
			if (const std::shared_ptr<execution::Condition> condition = changed.lock())
			{
				condition->notify();
			}
		});
}

TaskInjectStorage::~TaskInjectStorage()
{
	m_storageProvider->removeChangeListener(m_changeListener);
}

exec::task<void> TaskInjectStorage::run()
{
	while (true)
	{
		// Waits until a storage is available or the producers are done, holding no
		// thread. When done, keep draining the remainder (READY while count >= 1); end
		// once done AND empty.
		std::optional<StorageProvider::WaitResult> result;
		co_await m_storagesChanged->until([&]() {
			result = m_storageProvider->countOrDone(1);
			return result.has_value();
		});
		if (*result == StorageProvider::WaitResult::DONE)
		{
			if (!m_summaryLogged)
			{
				m_summaryLogged = true;
				LOG_INFO(
					"storage writer: " + std::to_string(m_injectCount) + " injects (" +
					std::to_string(m_injectedSourceLocationCount) + " source locations), busy " +
					std::to_string(m_injectBusyMs) + " ms");
			}
			co_return;	// no more work will arrive
		}

		std::shared_ptr<IntermediateStorage> source = m_storageProvider->consumeLargestStorage();
		if (!source)
		{
			continue;	// lost the race against another consumer -- retry
		}
		const std::shared_ptr<Storage> target = m_target.lock();
		if (!target)
		{
			co_return;	// the target storage is gone -- nothing left to do
		}
		const size_t sourceLocationCount = source->getSourceLocationCount();
		const TimeStamp start = TimeStamp::now();
#ifdef SOURCETRAIL_TURSO_CONCURRENT
		auto* persistent = dynamic_cast<PersistentStorage*>(target.get());
		if (persistent != nullptr && persistent->isConcurrentTursoSoleWriter())
		{
			// Fan-out S4: the concurrent Turso writer is the sole ingest
			// path — the serial SQLite inject is skipped; the drained result
			// is exported to SQLite in finishConcurrentTurso().
			persistent->submitToConcurrentTurso(*source);
		}
		else
		{
			target->inject(source.get());
			// Piggyback: also mirror this storage into the concurrent Turso writer.
			if (persistent != nullptr)
			{
				persistent->submitToConcurrentTurso(*source);
			}
		}
#else
		target->inject(source.get());
#endif
		m_injectBusyMs += static_cast<long long>(TimeStamp::now().deltaMS(start));
		m_injectCount++;
		m_injectedSourceLocationCount += sourceLocationCount;
	}
}

void TaskInjectStorage::handleMessage(MessageIndexingInterrupted*  /*message*/)
{
	m_storageProvider->clear();
}
