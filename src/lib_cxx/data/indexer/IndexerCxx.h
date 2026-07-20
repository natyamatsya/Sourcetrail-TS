#ifndef INDEXER_CXX_H
#define INDEXER_CXX_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "Indexer.h"
#include "IndexerCommandCxx.h"
#endif

SRCTRL_EXPORT class IndexerCxx: public Indexer<IndexerCommandCxx>
{
private:
	void doIndex(
		const std::shared_ptr<const IndexerCommandCxx>& indexerCommand,
		const std::shared_ptr<ParserClientImpl>& parserClient,
		const std::shared_ptr<IndexerStateInfo>& indexerStateInfo) override;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerCxx.inl"
#endif

#endif	  // INDEXER_CXX_H
