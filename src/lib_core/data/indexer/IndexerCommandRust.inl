// Inline implementations for IndexerCommandRust.h. Included at the end of that header (classic) or
// via the srctrl.indexer wrapper (purview); not a standalone TU.

#pragma once

inline IndexerCommandType IndexerCommandRust::getStaticIndexerCommandType()
{
	return IndexerCommandType::INDEXER_COMMAND_RUST;
}

inline IndexerCommandRust::IndexerCommandRust(
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

inline IndexerCommandType IndexerCommandRust::getIndexerCommandType() const
{
	return getStaticIndexerCommandType();
}

inline std::size_t IndexerCommandRust::getByteSize(std::size_t /*stringSize*/) const
{
	return 0;
}

inline std::string IndexerCommandRust::getIndexerCommandHash() const
{
	return std::string();
}

inline const std::set<FilePath>& IndexerCommandRust::getIndexedPaths() const
{
	return m_indexedPaths;
}

inline const FilePath& IndexerCommandRust::getWorkingDirectory() const
{
	return m_workingDirectory;
}

inline const std::vector<std::string>& IndexerCommandRust::getFeatures() const
{
	return m_features;
}

inline bool IndexerCommandRust::getAllFeatures() const
{
	return m_allFeatures;
}

inline bool IndexerCommandRust::getNoDefaultFeatures() const
{
	return m_noDefaultFeatures;
}

inline const std::string& IndexerCommandRust::getTargetTriple() const
{
	return m_targetTriple;
}

inline const std::string& IndexerCommandRust::getSpecializationScope() const
{
	return m_specializationScope;
}

inline bool IndexerCommandRust::getRestrictToPackage() const
{
	return m_restrictToPackage;
}
