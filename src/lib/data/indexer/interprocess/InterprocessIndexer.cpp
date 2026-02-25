#include "InterprocessIndexer.h"

#include "FileRegister.h"
#include "IndexerCommand.h"
#include "IndexerComposite.h"
#include "IndexerCommandType.h"
#include "LanguagePackageManager.h"
#include "ScopedFunctor.h"
#include "language_packages.h"
#include "logging.h"

#include <memory>
#include <type_traits>
#include <typeinfo>

#if defined(__GNUG__)
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace
{
std::string getExceptionTypeName(const std::exception& exception)
{
#if defined(__GNUG__)
	int status = 0;
	std::unique_ptr<char, decltype(&std::free)> demangledName(
		abi::__cxa_demangle(typeid(exception).name(), nullptr, nullptr, &status), &std::free);
	if (status == 0 && demangledName)
		return demangledName.get();
#endif

	return typeid(exception).name();
}

template <typename TResult, typename TCallable>
std::expected<TResult, std::string> expectedFromExceptions(
	const ProcessId processId,
	const std::string& context,
	TCallable&& callable)
{
	try
	{
		if constexpr (std::is_void_v<TResult>)
		{
			std::forward<TCallable>(callable)();
			return {};
		}
		else
		{
			return std::forward<TCallable>(callable)();
		}
	}
	catch (const std::exception& e)
	{
		const std::string message =
			context + " [" + getExceptionTypeName(e) + "]: " + std::string(e.what());
		LOG_ERROR_STREAM(<< processId << " " << message);
		return std::unexpected(message);
	}
	catch (...)
	{
		const std::string message = context + " [unknown exception type]";
		LOG_ERROR_STREAM(<< processId << " " << message);
		return std::unexpected(message);
	}
}
} // namespace

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

	return expectedFromExceptions<void>(
		m_processId,
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
						m_interprocessIntermediateStorageManager.getIntermediateStorageCount();
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
				const auto indexResult =
					expectedFromExceptions<std::shared_ptr<IntermediateStorage>>(
						m_processId,
						"exception while indexing \"" + indexerCommand->getSourceFilePath().str() +
							"\"",
						[&]() { return indexer->index(indexerCommand); });
				if (!indexResult)
				{
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
}
