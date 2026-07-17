#ifndef INDEXER_COMMAND_SWIFT_H
#define INDEXER_COMMAND_SWIFT_H

#include <set>
#include <string>
#include <vector>

#include "IndexerCommand.h"

class IndexerCommandSwift: public IndexerCommand
{
public:
	static IndexerCommandType getStaticIndexerCommandType();

	IndexerCommandSwift(
		const FilePath& sourceFilePath,
		const std::set<FilePath>& indexedPaths,
		const FilePath& workingDirectory,
		const std::vector<std::string>& buildArgs = {},
		const std::string& toolchainPath = "",
		const std::string& indexStorePath = "");

	IndexerCommandType getIndexerCommandType() const override;

	const std::set<FilePath>& getIndexedPaths() const;
	const FilePath& getWorkingDirectory() const;

	// Swift project-model options (SW5). See indexer_command.fbs.
	const std::vector<std::string>& getBuildArgs() const;
	const std::string& getToolchainPath() const;
	const std::string& getIndexStorePath() const;

protected:
	QJsonObject doSerialize() const override;

private:
	std::set<FilePath> m_indexedPaths;
	FilePath m_workingDirectory;
	std::vector<std::string> m_buildArgs;
	std::string m_toolchainPath;
	std::string m_indexStorePath;
};

#endif	  // INDEXER_COMMAND_SWIFT_H
