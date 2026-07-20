#ifndef INDEXER_COMPOSITE_H
#define INDEXER_COMPOSITE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <map>
#include <memory>

#include "IndexerBase.h"
#endif

SRCTRL_EXPORT class IndexerComposite: public IndexerBase
{
public:
	~IndexerComposite() override;

	IndexerCommandType getSupportedIndexerCommandType() const override;

	void addIndexer(std::shared_ptr<IndexerBase> indexer);

	IndexerBase::IndexResult index(const std::shared_ptr<IndexerCommand>& indexerCommand) override;

	void interrupt() override;

private:
	std::map<IndexerCommandType, std::shared_ptr<IndexerBase>> m_indexers;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerComposite.inl"
#endif

#endif	  // INDEXER_COMPOSITE_H
