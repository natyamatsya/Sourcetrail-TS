#include "SourceGroup.h"

#include "FilePath.h"
#include "FilePathFilter.h"
#include "IndexerCommand.h"
#include "MemoryIndexerCommandProvider.h"
#include "ProjectSettings.h"
#include "RefreshInfo.h"
#include "SourceGroupSettings.h"
#include "TaskLambda.h"

std::shared_ptr<IndexerCommandProvider> SourceGroup::getIndexerCommandProvider(const RefreshInfo& info) const
{
	return std::make_shared<MemoryIndexerCommandProvider>(getIndexerCommands(info));
}

std::vector<std::pair<FilePath, std::string>> SourceGroup::getSourceFileCommandHashes(
	const RefreshInfo& info) const
{
	// Consumed by iteration only (never point-looked-up), so a flat vector is
	// cheaper to build and iterate than a node-based map. One command per source
	// file within a group keeps the paths unique.
	std::vector<std::pair<FilePath, std::string>> hashes;
	for (const std::shared_ptr<IndexerCommand>& command: getIndexerCommands(info))
	{
		std::string hash = command->getIndexerCommandHash();
		if (!hash.empty())
		{
			hashes.emplace_back(command->getSourceFilePath(), std::move(hash));
		}
	}
	return hashes;
}

std::vector<std::pair<FilePath, std::string>> SourceGroup::getAllSourceFileCommandHashes() const
{
	RefreshInfo info;
	info.filesToIndex = getAllSourceFilePaths();
	info.mode = RefreshMode::ALL_FILES;
	return getSourceFileCommandHashes(info);
}

std::shared_ptr<Task> SourceGroup::getPreIndexTask(
	std::shared_ptr<StorageProvider>  /*storageProvider*/, std::shared_ptr<DialogView>  /*dialogView*/) const
{
	return std::make_shared<TaskLambda>([]() {});
}

std::optional<BuildModelSnapshot> SourceGroup::getBuildModelSnapshot() const
{
	return std::nullopt;
}

SourceGroupType SourceGroup::getType() const
{
	return getSourceGroupSettings()->getType();
}

LanguageType SourceGroup::getLanguage() const
{
	return getSourceGroupSettings()->getLanguage();
}

SourceGroupStatusType SourceGroup::getStatus() const
{
	return getSourceGroupSettings()->getStatus();
}

std::string SourceGroup::getSourceGroupId() const
{
	return getSourceGroupSettings()->getId();
}

std::expected<void, PrepareIndexingError> SourceGroup::prepareIndexing()
{
	return {};
}

bool SourceGroup::allowsPartialClearing() const
{
	return true;
}

std::set<FilePath> SourceGroup::filterToContainedSourceFilePath(
	const std::set<FilePath>& sourceFilePaths) const
{
	std::set<FilePath> filteredSourceFilePaths;
	for (const FilePath& sourceFilePath: getAllSourceFilePaths())
	{
		if (sourceFilePaths.find(sourceFilePath) == sourceFilePaths.end())
		{
			filteredSourceFilePaths.insert(sourceFilePath);
		}
	}
	return filteredSourceFilePaths;
}

bool SourceGroup::containsSourceFilePath(const FilePath& sourceFilePath) const
{
	return !filterToContainedSourceFilePath({sourceFilePath}).empty();
}

std::set<FilePath> SourceGroup::filterToContainedFilePaths(
	const std::set<FilePath>& filePaths,
	const std::set<FilePath>& indexedFilePaths,
	const std::set<FilePath>& indexedFileOrDirectoryPaths,
	const std::vector<FilePathFilter>& excludeFilters) 
{
	std::set<FilePath> containedFilePaths;

	for (const FilePath& filePath: filePaths)
	{
		bool isInIndexedPaths = false;

		for (const FilePath& indexedFileOrDirectoryPath: indexedFileOrDirectoryPaths)
		{
			if (indexedFileOrDirectoryPath == filePath ||
				indexedFileOrDirectoryPath.contains(filePath))
			{
				isInIndexedPaths = true;
				break;
			}
		}

		if (!isInIndexedPaths && indexedFilePaths.find(filePath) != indexedFilePaths.end())
		{
			isInIndexedPaths = true;
		}

		if (isInIndexedPaths)
		{
			isInIndexedPaths = !FilePathFilter::areMatching(excludeFilters, filePath);
		}

		if (isInIndexedPaths)
		{
			containedFilePaths.insert(filePath);
		}
	}

	return containedFilePaths;
}
