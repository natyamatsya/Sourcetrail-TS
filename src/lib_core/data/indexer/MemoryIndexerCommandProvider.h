#ifndef MEMORY_INDEXER_COMMAND_PROVIDER_H
#define MEMORY_INDEXER_COMMAND_PROVIDER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <map>

#include "IndexerCommandProvider.h"
#endif

SRCTRL_EXPORT class MemoryIndexerCommandProvider: public IndexerCommandProvider
{
public:
	MemoryIndexerCommandProvider(const std::vector<std::shared_ptr<IndexerCommand>>& commands);
	std::vector<FilePath> getAllSourceFilePaths() const override;
	std::shared_ptr<IndexerCommand> consumeCommand() override;
	std::shared_ptr<IndexerCommand> consumeCommandForSourceFilePath(const FilePath& filePath) override;
	std::vector<std::shared_ptr<IndexerCommand>> consumeAllCommands() override;
	void clear() override;
	size_t size() const override;

private:
	std::map<FilePath, std::shared_ptr<IndexerCommand>> m_commands;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "MemoryIndexerCommandProvider.inl"
#endif

#endif	  // MEMORY_INDEXER_COMMAND_PROVIDER_H
