// Inline implementations for IndexerCxx.h. Included at the end of that header (classic) or via
// the srctrl.cxx:frontend wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxParser.h"
#include "FileRegister.h"
#endif

inline void IndexerCxx::doIndex(
	const std::shared_ptr<const IndexerCommandCxx>& indexerCommand,
	const std::shared_ptr<ParserClientImpl>& parserClient,
	const std::shared_ptr<IndexerStateInfo>& indexerStateInfo)
{
	CxxParser parser(
		*parserClient,
		std::make_shared<FileRegister>(
			indexerCommand->getSourceFilePath(),
			indexerCommand->getIndexedPaths(),
			indexerCommand->getExcludeFilters()),
		indexerStateInfo);

	parser.buildIndex(indexerCommand);
}
