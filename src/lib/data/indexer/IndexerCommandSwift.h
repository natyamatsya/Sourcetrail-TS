#ifndef INDEXER_COMMAND_SWIFT_H
#define INDEXER_COMMAND_SWIFT_H

#include <set>
#include <string>

#include "IndexerCommand.h"

class IndexerCommandSwift: public IndexerCommand
{
public:
	static IndexerCommandType getStaticIndexerCommandType();

	IndexerCommandSwift(
		const FilePath& sourceFilePath,
		const std::set<FilePath>& indexedPaths,
		const FilePath& workingDirectory);

	IndexerCommandType getIndexerCommandType() const override;

	const std::set<FilePath>& getIndexedPaths() const;
	const FilePath& getWorkingDirectory() const;

protected:
	QJsonObject doSerialize() const override;

private:
	std::set<FilePath> m_indexedPaths;
	FilePath m_workingDirectory;
};

#endif	  // INDEXER_COMMAND_SWIFT_H
