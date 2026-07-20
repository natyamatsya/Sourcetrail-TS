#ifndef INDEXER_COMMAND_PROVIDER_H
#define INDEXER_COMMAND_PROVIDER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>
#include <vector>

class FilePath;
class IndexerCommand;
#endif

SRCTRL_EXPORT class IndexerCommandProvider
{
public:
	virtual ~IndexerCommandProvider() = default;
	virtual std::vector<FilePath> getAllSourceFilePaths() const = 0;
	virtual std::shared_ptr<IndexerCommand> consumeCommand() = 0;
	virtual std::shared_ptr<IndexerCommand> consumeCommandForSourceFilePath(const FilePath& filePath) = 0;
	virtual std::vector<std::shared_ptr<IndexerCommand>> consumeAllCommands() = 0;
	virtual void clear() = 0;
	virtual size_t size() const = 0;
	bool empty() const;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerCommandProvider.inl"
#endif

#endif	  // INDEXER_COMMAND_PROVIDER_H
