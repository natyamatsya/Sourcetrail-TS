// Inline implementations for IndexerComposite.h. Included at the end of that header (classic) or via
// the srctrl.indexer wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerCommand.h"
#include "IntermediateStorage.h"
#include "logging.h"
#endif

inline IndexerComposite::~IndexerComposite() = default;

inline IndexerCommandType IndexerComposite::getSupportedIndexerCommandType() const
{
	return IndexerCommandType::INDEXER_COMMAND_UNKNOWN;
}

inline void IndexerComposite::addIndexer(std::shared_ptr<IndexerBase> indexer)
{
	m_indexers.emplace(indexer->getSupportedIndexerCommandType(), indexer);
}

inline IndexerBase::IndexResult IndexerComposite::index(const std::shared_ptr<IndexerCommand>& indexerCommand)
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

inline void IndexerComposite::interrupt()
{
	for (auto& it: m_indexers)
	{
		it.second->interrupt();
	}
}
