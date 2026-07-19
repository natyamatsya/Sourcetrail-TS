#include "IndexerComposite.h"

#include "IndexerCommand.h"
#include "IntermediateStorage.h"
#include "logging.h"

IndexerComposite::~IndexerComposite() = default;

IndexerCommandType IndexerComposite::getSupportedIndexerCommandType() const
{
	return IndexerCommandType::INDEXER_COMMAND_UNKNOWN;
}

void IndexerComposite::addIndexer(std::shared_ptr<IndexerBase> indexer)
{
	m_indexers.emplace(indexer->getSupportedIndexerCommandType(), indexer);
}

IndexerBase::IndexResult IndexerComposite::index(const std::shared_ptr<IndexerCommand>& indexerCommand)
{
	if (!indexerCommand)
	{
		const std::string message = "No indexer command provided.";
		LOG_ERROR(message);
		return std::unexpected(
			utility::makeExpectedError(IndexerErrorCode::NoCommandProvided, message));
	}

	const auto it = m_indexers.find(indexerCommand->getIndexerCommandType());
	if (it == m_indexers.end())
	{
		const std::string message =
			"No indexer found that supports \"" +
			indexerCommandTypeToString(indexerCommand->getIndexerCommandType()) + "\".";
		LOG_ERROR(message);
		return std::unexpected(
			utility::makeExpectedError(IndexerErrorCode::NoIndexerForCommand, message));
	}

	return it->second->index(indexerCommand);
}

void IndexerComposite::interrupt()
{
	for (auto& it: m_indexers)
	{
		it.second->interrupt();
	}
}
