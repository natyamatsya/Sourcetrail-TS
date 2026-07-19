#ifndef INDEXER_BASE_H
#define INDEXER_BASE_H

#include <memory>

#include "IndexerCommandType.h"
#include "utilityExpected.h"

class FileRegister;
class IndexerCommand;
class IntermediateStorage;

enum class IndexerErrorCode
{
	NoCommandProvided,
	UnsupportedCommand,
	NoIndexerForCommand,
	ExecutionException,
	ExecutionUnknownException,
	Interrupted
};

using IndexerError = utility::ExpectedError<IndexerErrorCode>;

class IndexerBase
{
public:
	using IndexResult = std::expected<std::shared_ptr<IntermediateStorage>, IndexerError>;

	IndexerBase();
	virtual ~IndexerBase() = default;

	virtual IndexerCommandType getSupportedIndexerCommandType() const = 0;
	virtual IndexResult index(const std::shared_ptr<IndexerCommand>& indexerCommand) = 0;
	virtual void interrupt() = 0;
};

#endif	  // INDEXER_BASE_H
