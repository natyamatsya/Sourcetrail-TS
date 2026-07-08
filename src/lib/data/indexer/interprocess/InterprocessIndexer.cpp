#include "InterprocessIndexer.h"

#include "FileRegister.h"
#include "IndexerCommand.h"
#include "IndexerComposite.h"
#include "IndexerCommandType.h"
#include "LanguagePackageManager.h"
#include "ScopedFunctor.h"
#include "language_package_flags.h"
#include "logging.h"
#include "utilityExpected.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>

InterprocessIndexer::InterprocessIndexer(const std::string& uuid, ProcessId processId)
	: m_interprocessIndexerCommandManager(uuid, processId, false)
	, m_interprocessIndexingStatusManager(uuid, processId, false)
	, m_interprocessIntermediateStorageManager(uuid, processId, false)
	, m_uuid(uuid)
	, m_processId(processId)
{
}

InterprocessIndexer::WorkResult InterprocessIndexer::work()
{
	using enum IndexerCommandType;
	std::atomic<bool> updaterThreadRunning = true;
	// Wakes the updater thread's poll so shutdown returns at once instead of
	// blocking on its interval, and makes the flag access race-free.
	std::mutex updaterMutex;
	std::condition_variable updaterCondition;
	std::shared_ptr<std::thread> updaterThread;
	std::shared_ptr<IndexerBase> indexer;

	[[maybe_unused]]
	ScopedFunctor threadStopper([&]()
	{
		{
			std::lock_guard<std::mutex> lock(updaterMutex);
			updaterThreadRunning = false;
		}
		updaterCondition.notify_all();
		if (!updaterThread)
			return;

		updaterThread->join();
		updaterThread.reset();
	});

	[[maybe_unused]]
	ScopedFunctor shutdownLogger([&]() { LOG_INFO_STREAM(<< m_processId << " shutting down indexer"); });

	const WorkResult workResult = utility::expectedFromExceptions<void>(
		InterprocessIndexerErrorCode::ExecutionException,
		InterprocessIndexerErrorCode::ExecutionUnknownException,
		"error while running indexer worker",
		[&]()
		{
			LOG_INFO_STREAM(<< m_processId << " starting up indexer");
			indexer = LanguagePackageManager::getInstance()->instantiateSupportedIndexers();

			updaterThread = std::make_shared<std::thread>([&]() {
				while (updaterThreadRunning)
				{
					// Poll the cross-process interrupt flag, but wake immediately on
					// shutdown. 100ms keeps interrupt/cancel latency low (was 1s) while
					// staying negligible on CPU.
					{
						std::unique_lock<std::mutex> lock(updaterMutex);
						updaterCondition.wait_for(
							lock,
							std::chrono::milliseconds(100),
							[&]() { return !updaterThreadRunning; });
					}
					if (!updaterThreadRunning)
						break;

					if (!m_interprocessIndexingStatusManager.getIndexingInterrupted())
						continue;

					LOG_INFO_STREAM(<< m_processId << " received indexer interrupt command.");
					if (indexer)
						indexer->interrupt();
					updaterThreadRunning = false;
				}
			});

			std::set<IndexerCommandType> skipTypes;
			if constexpr (language_packages::buildRustLanguagePackage)
				skipTypes.insert(INDEXER_COMMAND_RUST);
			if constexpr (language_packages::buildSwiftLanguagePackage)
				skipTypes.insert(INDEXER_COMMAND_SWIFT);
			while (true)
			{
				// Block until a command is pushed (immediate wake) instead of
				// polling. The 500ms timeout is a liveness backstop so the loop
				// re-checks the stop flags even if a notify is ever missed; the
				// main process also notifies on stop/interrupt for a prompt exit.
				const std::shared_ptr<IndexerCommand> indexerCommand =
					m_interprocessIndexerCommandManager.popIndexerCommandBlocking(skipTypes, 500);

				if (!indexerCommand)
				{
					if (m_interprocessIndexingStatusManager.getIndexingInterrupted() ||
						m_interprocessIndexingStatusManager.getQueueStopped())
						return;
					continue;
				}

				LOG_INFO_STREAM(
					<< m_processId << " fetched indexer command for \""
					<< indexerCommand->getSourceFilePath().str() << "\"");
				LOG_INFO_STREAM(
					<< m_processId << " indexer commands left: "
					<< m_interprocessIndexerCommandManager.indexerCommandCount());

				while (updaterThreadRunning)
				{
					const size_t storageCount =
						m_interprocessIntermediateStorageManager.peekCount();
					if (storageCount < 2)
						break;

					LOG_INFO_STREAM(
						<< m_processId << " waits, too many intermediate storages: " << storageCount);

					std::this_thread::sleep_for(std::chrono::milliseconds(200));
				}

				if (!updaterThreadRunning)
					return;

				LOG_INFO_STREAM(
					<< m_processId << " updating indexer status with currently indexed filepath");
				m_interprocessIndexingStatusManager.startIndexingSourceFile(
					indexerCommand->getSourceFilePath());

				LOG_INFO_STREAM(<< m_processId << " starting to index current file");
				const IndexerBase::IndexResult indexResult = indexer->index(indexerCommand);
				if (!indexResult)
				{
					LOG_ERROR_STREAM(
						<< m_processId << " " << indexResult.error());
					LOG_ERROR_STREAM(
						<< m_processId << " failing indexer command payload: "
						<< IndexerCommand::serialize(indexerCommand));
					LOG_ERROR_STREAM(
						<< m_processId
						<< " keeping current file marked as crashed and continuing with next command.");
					LOG_INFO_STREAM(<< m_processId << " all done");
					continue;
				}

				if (*indexResult)
				{
					LOG_INFO_STREAM(<< m_processId << " pushing index to shared memory");
					m_interprocessIntermediateStorageManager.pushIntermediateStorage(*indexResult);
				}

				LOG_INFO_STREAM(<< m_processId << " finalizing indexer status for current file");
				m_interprocessIndexingStatusManager.finishIndexingSourceFile();

				LOG_INFO_STREAM(<< m_processId << " all done");
			}
		});

	if (!workResult)
		LOG_ERROR_STREAM(<< m_processId << " " << workResult.error());

	return workResult;
}
