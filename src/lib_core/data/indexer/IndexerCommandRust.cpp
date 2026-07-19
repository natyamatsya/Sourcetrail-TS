#include "IndexerCommandRust.h"

IndexerCommandType IndexerCommandRust::getStaticIndexerCommandType()
{
	return IndexerCommandType::INDEXER_COMMAND_RUST;
}

IndexerCommandRust::IndexerCommandRust(
	const std::set<FilePath>& indexedPaths,
	const FilePath& workingDirectory,
	const std::vector<std::string>& features,
	bool allFeatures,
	bool noDefaultFeatures,
	const std::string& targetTriple,
	const std::string& specializationScope,
	bool restrictToPackage)
	: m_indexedPaths(indexedPaths)
	, m_workingDirectory(workingDirectory)
	, m_features(features)
	, m_allFeatures(allFeatures)
	, m_noDefaultFeatures(noDefaultFeatures)
	, m_targetTriple(targetTriple)
	, m_specializationScope(specializationScope)
	, m_restrictToPackage(restrictToPackage)
{
}

IndexerCommandType IndexerCommandRust::getIndexerCommandType() const
{
	return getStaticIndexerCommandType();
}

std::size_t IndexerCommandRust::getByteSize(std::size_t /*stringSize*/) const
{
	return 0;
}

std::string IndexerCommandRust::getIndexerCommandHash() const
{
	return std::string();
}

const std::set<FilePath>& IndexerCommandRust::getIndexedPaths() const
{
	return m_indexedPaths;
}

const FilePath& IndexerCommandRust::getWorkingDirectory() const
{
	return m_workingDirectory;
}

const std::vector<std::string>& IndexerCommandRust::getFeatures() const
{
	return m_features;
}

bool IndexerCommandRust::getAllFeatures() const
{
	return m_allFeatures;
}

bool IndexerCommandRust::getNoDefaultFeatures() const
{
	return m_noDefaultFeatures;
}

const std::string& IndexerCommandRust::getTargetTriple() const
{
	return m_targetTriple;
}

const std::string& IndexerCommandRust::getSpecializationScope() const
{
	return m_specializationScope;
}

bool IndexerCommandRust::getRestrictToPackage() const
{
	return m_restrictToPackage;
}
