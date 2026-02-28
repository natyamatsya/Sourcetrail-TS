#include "SourceGroupSwift.h"

#if BUILD_SWIFT_LANGUAGE_PACKAGE

#include "FileManager.h"
#include "IndexerCommandSwift.h"
#include "RefreshInfo.h"
#include "SourceGroupSettingsSwiftEmpty.h"
#include "utility.h"

SourceGroupSwift::SourceGroupSwift(const std::shared_ptr<SourceGroupSettingsSwiftEmpty>& settings)
	: m_settings{settings}
{
}

bool SourceGroupSwift::prepareIndexing()
{
	return true;
}

std::set<FilePath> SourceGroupSwift::filterToContainedFilePaths(
	const std::set<FilePath>& filePaths) const
{
	return filterToContainedSourceFilePath(filePaths);
}

std::set<FilePath> SourceGroupSwift::getAllSourceFilePaths() const
{
	FileManager fileManager;
	fileManager.update(
		m_settings->getSourcePathsExpandedAndAbsolute(),
		m_settings->getExcludeFiltersExpandedAndAbsolute(),
		m_settings->getSourceExtensions());
	return fileManager.getAllSourceFilePaths();
}

std::vector<std::shared_ptr<IndexerCommand>> SourceGroupSwift::getIndexerCommands(
	const RefreshInfo& info) const
{
	const std::set<FilePath> indexedPaths =
		utility::toSet(m_settings->getSourcePathsExpandedAndAbsolute());
	const FilePath workingDir = m_settings->getProjectDirectoryPath();

	if (!info.filesToIndex.empty())
		return {std::make_shared<IndexerCommandSwift>(workingDir, indexedPaths, workingDir)};
	return {};
}

std::shared_ptr<SourceGroupSettings> SourceGroupSwift::getSourceGroupSettings()
{
	return m_settings;
}

std::shared_ptr<const SourceGroupSettings> SourceGroupSwift::getSourceGroupSettings() const
{
	return m_settings;
}

#endif	  // BUILD_SWIFT_LANGUAGE_PACKAGE
