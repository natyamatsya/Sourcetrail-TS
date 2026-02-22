#ifndef INDEXER_COMMAND_RUST_H
#define INDEXER_COMMAND_RUST_H

#include "language_packages.h"

#if BUILD_RUST_LANGUAGE_PACKAGE

#include <set>
#include <string>

#include "IndexerCommand.h"

class IndexerCommandRust: public IndexerCommand
{
public:
	static IndexerCommandType getStaticIndexerCommandType();

	IndexerCommandRust(
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

#endif	  // BUILD_RUST_LANGUAGE_PACKAGE

#endif	  // INDEXER_COMMAND_RUST_H
