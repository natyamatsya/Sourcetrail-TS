#include "IndexerCommandZig.h"

IndexerCommandType IndexerCommandZig::getStaticIndexerCommandType()
{
	return IndexerCommandType::INDEXER_COMMAND_ZIG;
}

IndexerCommandZig::IndexerCommandZig(
	const std::set<FilePath>& indexedPaths, const FilePath& workingDirectory)
	: m_indexedPaths(indexedPaths), m_workingDirectory(workingDirectory)
{
}

IndexerCommandType IndexerCommandZig::getIndexerCommandType() const
{
	return getStaticIndexerCommandType();
}

std::size_t IndexerCommandZig::getByteSize(std::size_t /*stringSize*/) const
{
	return 0;
}

std::string IndexerCommandZig::getIndexerCommandHash() const
{
	return std::string();
}

const std::set<FilePath>& IndexerCommandZig::getIndexedPaths() const
{
	return m_indexedPaths;
}

const FilePath& IndexerCommandZig::getWorkingDirectory() const
{
	return m_workingDirectory;
}
