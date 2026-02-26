#include "InterprocessIndexer.h"

#include "FileRegister.h"
#include "IndexerCommand.h"
#include "IndexerComposite.h"
#include "IndexerCommandType.h"
#include "LanguagePackageManager.h"
#include "ScopedFunctor.h"
#include "language_packages.h"
#include "logging.h"
#include "utilityExpected.h"

#include <memory>

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
	bool updaterThreadRunning = true;
	std::shared_ptr<std::thread> updaterThread;
	std::shared_ptr<IndexerBase> indexer;

	[[maybe_unused]]
	ScopedFunctor threadStopper([&]()
	{
		updaterThreadRunning = false;
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
					std::this_thread::sleep_for(std::chrono::milliseconds(1000));

					if (!m_interprocessIndexingStatusManager.getIndexingInterrupted())
						continue;

					LOG_INFO_STREAM(<< m_processId << " received indexer interrupt command.");
					if (indexer)
						indexer->interrupt();
					updaterThreadRunning = false;
				}
			});

			const IndexerCommandType skipType =
#if BUILD_RUST_LANGUAGE_PACKAGE
				INDEXER_COMMAND_RUST;
#else
				INDEXER_COMMAND_UNKNOWN;
#endif
			while (true)
			{
				const std::shared_ptr<IndexerCommand> indexerCommand =
					m_interprocessIndexerCommandManager.popIndexerCommand(skipType);

				if (!indexerCommand)
				{
					if (m_interprocessIndexingStatusManager.getIndexingInterrupted() ||
						m_interprocessIndexingStatusManager.getQueueStopped())
						return;
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
