#ifndef COMBINED_INDEXER_COMMAND_PROVIDER_H
#define COMBINED_INDEXER_COMMAND_PROVIDER_H

#include <string>
#include <utility>
#include <vector>

#include "IndexerCommandProvider.h"

class CombinedIndexerCommandProvider: public IndexerCommandProvider
{
public:
	//! Commands consumed from this provider are tagged with sourceGroupId
	//! (fan-out S1). Tagging happens here — the single consumption choke point —
	//! because the C++ source groups hand out lazy providers whose commands only
	//! materialize on consume.
	void addProvider(std::shared_ptr<IndexerCommandProvider> provider, const std::string& sourceGroupId);

	std::vector<FilePath> getAllSourceFilePaths() const override;
	std::shared_ptr<IndexerCommand> consumeCommand() override;
	std::shared_ptr<IndexerCommand> consumeCommandForSourceFilePath(const FilePath& filePath) override;
	std::vector<std::shared_ptr<IndexerCommand>> consumeAllCommands() override;

	void clear() override;
	size_t size() const override;

private:
	std::vector<std::pair<std::shared_ptr<IndexerCommandProvider>, std::string>> m_providers;
};

#endif	  // COMBINED_INDEXER_COMMAND_PROVIDER_H
