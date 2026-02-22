#include "SourceGroupRust.h"

#if BUILD_RUST_LANGUAGE_PACKAGE

#include "FileManager.h"
#include "IndexerCommandRust.h"
#include "RefreshInfo.h"
#include "SourceGroupSettingsRustEmpty.h"
#include "utility.h"

SourceGroupRust::SourceGroupRust(std::shared_ptr<SourceGroupSettingsRustEmpty> settings)
	: m_settings(std::move(settings))
{
}

bool SourceGroupRust::prepareIndexing()
{
	return true;
}

std::set<FilePath> SourceGroupRust::filterToContainedFilePaths(
	const std::set<FilePath>& filePaths) const
{
	return filterToContainedSourceFilePath(filePaths);
}

std::set<FilePath> SourceGroupRust::getAllSourceFilePaths() const
{
	FileManager fileManager;
	fileManager.update(
		m_settings->getSourcePathsExpandedAndAbsolute(),
		m_settings->getExcludeFiltersExpandedAndAbsolute(),
		m_settings->getSourceExtensions());
	return fileManager.getAllSourceFilePaths();
}

std::vector<std::shared_ptr<IndexerCommand>> SourceGroupRust::getIndexerCommands(
	const RefreshInfo& info) const
{
	const std::set<FilePath> indexedPaths =
		utility::toSet(m_settings->getSourcePathsExpandedAndAbsolute());
	const FilePath workingDir = m_settings->getProjectDirectoryPath();

	// The Rust indexer works at crate level via working_directory (Cargo.toml).
	// Emit one command per source group — the source file path is the working dir
	// itself (used only as a key for status tracking), not an individual .rs file.
	if (!info.filesToIndex.empty())
	{
		return {std::make_shared<IndexerCommandRust>(workingDir, indexedPaths, workingDir)};
	}
	return {};
}

std::shared_ptr<SourceGroupSettings> SourceGroupRust::getSourceGroupSettings()
{
	return m_settings;
}

std::shared_ptr<const SourceGroupSettings> SourceGroupRust::getSourceGroupSettings() const
{
	return m_settings;
}

#endif	  // BUILD_RUST_LANGUAGE_PACKAGE
