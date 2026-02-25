#ifndef INDEXER_H
#define INDEXER_H

#include <memory>

#include "IndexerBase.h"
#include "IndexerCommand.h"
#include "IndexerStateInfo.h"
#include "ParserClientImpl.h"
#include "logging.h"
#include "utilityExpected.h"

template <typename T>
class Indexer: public IndexerBase
{
public:
	Indexer();
	IndexerCommandType getSupportedIndexerCommandType() const override;
	IndexerBase::IndexResult index(const std::shared_ptr<IndexerCommand>& indexerCommand) override;
	void interrupt() override;

private:
	virtual void doIndex(
		const std::shared_ptr<T>& indexerCommand,
		const std::shared_ptr<ParserClientImpl>& parserClient,
		const std::shared_ptr<IndexerStateInfo>& indexerStateInfo) = 0;

	std::shared_ptr<IndexerStateInfo> m_indexerStateInfo;
};


template <typename T>
Indexer<T>::Indexer(): m_indexerStateInfo(std::make_shared<IndexerStateInfo>())
{
	m_indexerStateInfo->indexingInterrupted = false;
}

template <typename T>
IndexerCommandType Indexer<T>::getSupportedIndexerCommandType() const
{
	return T::getStaticIndexerCommandType();
}

template <typename T>
void Indexer<T>::interrupt()
{
	m_indexerStateInfo->indexingInterrupted = true;
}

template <typename T>
IndexerBase::IndexResult Indexer<T>::index(const std::shared_ptr<IndexerCommand>& indexerCommand)
{
	if (!indexerCommand)
	{
		const std::string message =
			"Trying to process null indexer command with indexer that supports \"" +
			indexerCommandTypeToString(getSupportedIndexerCommandType()) + "\".";
		LOG_ERROR(message);
		return std::unexpected(
			utility::makeExpectedError(IndexerErrorCode::NoCommandProvided, message));
	}

	const std::shared_ptr<T> castCommand = std::dynamic_pointer_cast<T>(indexerCommand);
	if (!castCommand)
	{
		const std::string message =
			"Trying to process " +
			indexerCommandTypeToString(indexerCommand->getIndexerCommandType()) +
			" indexer command with indexer that supports \"" +
			indexerCommandTypeToString(getSupportedIndexerCommandType()) + "\".";
		LOG_ERROR(message);
		return std::unexpected(
			utility::makeExpectedError(IndexerErrorCode::UnsupportedCommand, message));
	}

	const std::shared_ptr<IntermediateStorage> storage = std::make_shared<IntermediateStorage>();
	const std::shared_ptr<ParserClientImpl> parserClient = std::make_shared<ParserClientImpl>(storage);

	const auto doIndexResult = utility::expectedFromExceptions<void>(
		IndexerErrorCode::ExecutionException,
		IndexerErrorCode::ExecutionUnknownException,
		"error while running " +
			indexerCommandTypeToString(getSupportedIndexerCommandType()) +
			" indexer for \"" + castCommand->getSourceFilePath().str() + "\"",
		[&]() { doIndex(castCommand, parserClient, m_indexerStateInfo); });
	if (!doIndexResult)
		return std::unexpected(doIndexResult.error());

	if (storage->hasFatalErrors())
	{
		storage->setAllFilesIncomplete();
	}
	else
	{
		storage->setFilesWithErrorsIncomplete();
	}

	if (m_indexerStateInfo->indexingInterrupted)
		return std::unexpected(
			utility::makeExpectedError(IndexerErrorCode::Interrupted, "indexing interrupted"));

	return storage;
}

#endif	  // INDEXER_H
