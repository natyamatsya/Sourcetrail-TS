#ifndef INDEXER_CXX_H
#define INDEXER_CXX_H

#include "Indexer.h"
#include "IndexerCommandCxx.h"

class IndexerCxx: public Indexer<IndexerCommandCxx>
{
private:
	void doIndex(
		const std::shared_ptr<IndexerCommandCxx>& indexerCommand,
		const std::shared_ptr<ParserClientImpl>& parserClient,
		const std::shared_ptr<IndexerStateInfo>& indexerStateInfo) override;
};

#endif	  // INDEXER_CXX_H
