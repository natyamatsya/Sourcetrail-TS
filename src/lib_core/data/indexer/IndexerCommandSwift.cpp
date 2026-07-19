#include "IndexerCommandSwift.h"


IndexerCommandType IndexerCommandSwift::getStaticIndexerCommandType()
{
	return IndexerCommandType::INDEXER_COMMAND_SWIFT;
}

IndexerCommandSwift::IndexerCommandSwift(
	const FilePath& sourceFilePath,
	const std::set<FilePath>& indexedPaths,
	const FilePath& workingDirectory,
	const std::vector<std::string>& buildArgs,
	const std::string& toolchainPath,
	const std::string& indexStorePath,
	const std::string& specializationScope)
	: IndexerCommand(sourceFilePath)
	, m_indexedPaths(indexedPaths)
	, m_workingDirectory(workingDirectory)
	, m_buildArgs(buildArgs)
	, m_toolchainPath(toolchainPath)
	, m_indexStorePath(indexStorePath)
	, m_specializationScope(specializationScope)
{
}

IndexerCommandType IndexerCommandSwift::getIndexerCommandType() const
{
	return getStaticIndexerCommandType();
}

const std::set<FilePath>& IndexerCommandSwift::getIndexedPaths() const
{
	return m_indexedPaths;
}

const FilePath& IndexerCommandSwift::getWorkingDirectory() const
{
	return m_workingDirectory;
}

const std::vector<std::string>& IndexerCommandSwift::getBuildArgs() const
{
	return m_buildArgs;
}

const std::string& IndexerCommandSwift::getToolchainPath() const
{
	return m_toolchainPath;
}

const std::string& IndexerCommandSwift::getIndexStorePath() const
{
	return m_indexStorePath;
}

const std::string& IndexerCommandSwift::getSpecializationScope() const
{
	return m_specializationScope;
}
