// Inline implementations for IndexerCommandSwift.h. Included at the end of that header (classic) or
// via the srctrl.indexer wrapper (purview); not a standalone TU.

#pragma once

inline IndexerCommandType IndexerCommandSwift::getStaticIndexerCommandType()
{
	return IndexerCommandType::INDEXER_COMMAND_SWIFT;
}

inline IndexerCommandSwift::IndexerCommandSwift(
	const std::set<FilePath>& indexedPaths,
	const FilePath& workingDirectory,
	const std::vector<std::string>& buildArgs,
	const std::string& toolchainPath,
	const std::string& indexStorePath,
	const std::string& specializationScope)
	: m_indexedPaths(indexedPaths)
	, m_workingDirectory(workingDirectory)
	, m_buildArgs(buildArgs)
	, m_toolchainPath(toolchainPath)
	, m_indexStorePath(indexStorePath)
	, m_specializationScope(specializationScope)
{
}

inline IndexerCommandType IndexerCommandSwift::getIndexerCommandType() const
{
	return getStaticIndexerCommandType();
}

inline std::size_t IndexerCommandSwift::getByteSize(std::size_t /*stringSize*/) const
{
	return 0;
}

inline std::string IndexerCommandSwift::getIndexerCommandHash() const
{
	return std::string();
}

inline const std::set<FilePath>& IndexerCommandSwift::getIndexedPaths() const
{
	return m_indexedPaths;
}

inline const FilePath& IndexerCommandSwift::getWorkingDirectory() const
{
	return m_workingDirectory;
}

inline const std::vector<std::string>& IndexerCommandSwift::getBuildArgs() const
{
	return m_buildArgs;
}

inline const std::string& IndexerCommandSwift::getToolchainPath() const
{
	return m_toolchainPath;
}

inline const std::string& IndexerCommandSwift::getIndexStorePath() const
{
	return m_indexStorePath;
}

inline const std::string& IndexerCommandSwift::getSpecializationScope() const
{
	return m_specializationScope;
}
