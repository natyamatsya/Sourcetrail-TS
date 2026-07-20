#ifndef INTERPROCESS_INDEXER_H
#define INTERPROCESS_INDEXER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "InterprocessBackend.h"
#include "utilityExpected.h"

#include <expected>
#endif

SRCTRL_EXPORT enum class InterprocessIndexerErrorCode
{
	ExecutionException,
	ExecutionUnknownException
};

SRCTRL_EXPORT using InterprocessIndexerError = utility::ExpectedError<InterprocessIndexerErrorCode>;

SRCTRL_EXPORT class InterprocessIndexer
{
public:
	using WorkResult = std::expected<void, InterprocessIndexerError>;

	//! A non-empty onlyGroupId pins this indexer to commands of one source
	//! group (fan-out S2); empty processes commands of any group.
	InterprocessIndexer(
		const std::string& uuid, ProcessId processId, const std::string& onlyGroupId = std::string());

	WorkResult work();

private:
	IndexerCommandManagerImpl m_interprocessIndexerCommandManager;
	IndexingStatusManagerImpl m_interprocessIndexingStatusManager;
	IntermediateStorageManagerImpl m_interprocessIntermediateStorageManager;

	const std::string m_uuid;
	const ProcessId m_processId;
	const std::string m_onlyGroupId;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "InterprocessIndexer.inl"
#endif

#endif	  // INTERPROCESS_INDEXER_H
