// Inline implementations for IndexerCommandZig.h. Included at the end of that header (classic) or
// via the srctrl.indexer wrapper (purview); not a standalone TU.

#pragma once

inline IndexerCommandType IndexerCommandZig::getStaticIndexerCommandType()
{
	return IndexerCommandType::INDEXER_COMMAND_ZIG;
}

inline IndexerCommandZig::IndexerCommandZig(
	const std::set<FilePath>& indexedPaths, const FilePath& workingDirectory)
	: m_indexedPaths(indexedPaths), m_workingDirectory(workingDirectory)
{
}

inline IndexerCommandType IndexerCommandZig::getIndexerCommandType() const
{
	return getStaticIndexerCommandType();
}

inline std::size_t IndexerCommandZig::getByteSize(std::size_t /*stringSize*/) const
{
	return 0;
}

inline std::string IndexerCommandZig::getIndexerCommandHash() const
{
	return std::string();
}

inline const std::set<FilePath>& IndexerCommandZig::getIndexedPaths() const
{
	return m_indexedPaths;
}

inline const FilePath& IndexerCommandZig::getWorkingDirectory() const
{
	return m_workingDirectory;
}
