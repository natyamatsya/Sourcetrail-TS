#include "SourceGroupCustomCommand.h"
#ifndef SRCTRL_MODULE_BUILD
#include "IndexerCommand.h"
#include "FilePathFilter.h"
#endif

#ifndef SRCTRL_MODULE_BUILD
#include "FileManager.h"
#endif
#include "IndexerCommandCustom.h"
#ifndef SRCTRL_MODULE_BUILD
#include "ProjectSettings.h"
#endif
#include "RefreshInfo.h"
#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsCustomCommand.h"
#endif
#include "SqliteIndexStorage.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.indexer;
import srctrl.settings;
import srctrl.utility;
#endif

SourceGroupCustomCommand::SourceGroupCustomCommand(
	std::shared_ptr<SourceGroupSettingsCustomCommand> settings)
	: m_settings(settings)
{
}

bool SourceGroupCustomCommand::allowsPartialClearing() const
{
	return false;
}

std::set<FilePath> SourceGroupCustomCommand::filterToContainedFilePaths(
	const std::set<FilePath>& filePaths) const
{
	return SourceGroup::filterToContainedFilePaths(
		filePaths,
		std::set<FilePath>(),
		utility::toSet(m_settings->getSourcePathsExpandedAndAbsolute()),
		m_settings->getExcludeFiltersExpandedAndAbsolute());
}

std::set<FilePath> SourceGroupCustomCommand::getAllSourceFilePaths() const
{
	FileManager fileManager;
	fileManager.update(
		m_settings->getSourcePathsExpandedAndAbsolute(),
		m_settings->getExcludeFiltersExpandedAndAbsolute(),
		m_settings->getSourceExtensions());
	return fileManager.getAllSourceFilePaths();
}

std::vector<std::shared_ptr<IndexerCommand>> SourceGroupCustomCommand::getIndexerCommands(
	const RefreshInfo& info) const
{
	const bool runInParallel = m_settings->getRunInParallel();

	std::vector<std::shared_ptr<IndexerCommand>> indexerCommands;
	for (const FilePath& sourcePath: getAllSourceFilePaths())
	{
		if (info.filesToIndex.find(sourcePath) != info.filesToIndex.end())
		{
			indexerCommands.push_back(std::make_shared<IndexerCommand>(
				sourcePath,
				IndexerCommandCustom(
					m_settings->getCustomCommand(),
					std::vector<std::string> {},
					m_settings->getProjectSettings()->getProjectFilePath(),
					m_settings->getProjectSettings()->getTempDBFilePath(),
					std::to_string(SqliteIndexStorage::getStorageVersion()),
					sourcePath,
					runInParallel)));
		}
	}

	return indexerCommands;
}

std::shared_ptr<SourceGroupSettings> SourceGroupCustomCommand::getSourceGroupSettings()
{
	return m_settings;
}

std::shared_ptr<const SourceGroupSettings> SourceGroupCustomCommand::getSourceGroupSettings() const
{
	return m_settings;
}
