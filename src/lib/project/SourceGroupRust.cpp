#include "SourceGroupRust.h"

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

	std::vector<std::shared_ptr<IndexerCommand>> commands;
	for (const FilePath& sourcePath: getAllSourceFilePaths())
	{
		if (info.filesToIndex.find(sourcePath) != info.filesToIndex.end())
		{
			commands.push_back(
				std::make_shared<IndexerCommandRust>(sourcePath, indexedPaths, workingDir));
		}
	}
	return commands;
}

std::shared_ptr<SourceGroupSettings> SourceGroupRust::getSourceGroupSettings()
{
	return m_settings;
}

std::shared_ptr<const SourceGroupSettings> SourceGroupRust::getSourceGroupSettings() const
{
	return m_settings;
}
