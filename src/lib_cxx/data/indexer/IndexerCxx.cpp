#include "IndexerCxx.h"

#include "CxxParser.h"
#include "FileRegister.h"

void IndexerCxx::doIndex(
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
