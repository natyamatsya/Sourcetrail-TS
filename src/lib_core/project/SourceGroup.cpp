#include "SourceGroup.h"

#ifndef SRCTRL_MODULE_BUILD
#include "FilePath.h"
#include "FilePathFilter.h"
#include "IndexerCommand.h"
#include "MemoryIndexerCommandProvider.h"
#include "ProjectSettings.h"
#endif
#include "RefreshInfo.h"

#include <algorithm>
#include <map>

#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettings.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.indexer;
import srctrl.settings;
#endif

std::shared_ptr<IndexerCommandProvider> SourceGroup::getIndexerCommandProvider(const RefreshInfo& info) const
{
	return std::make_shared<MemoryIndexerCommandProvider>(getIndexerCommands(info));
}

std::vector<std::pair<FilePath, std::string>> SourceGroup::getSourceFileCommandHashes(
	const RefreshInfo& info) const
{
	// One hash per source file. A compilation database can compile a file more than
	// once (a library's objects for its static and its shared target): the file's hash
	// is then all of its commands' hashes, in a fixed order. With one hash per command,
	// the database kept the last one written, and every refresh found the others
	// "changed" and re-indexed the file and all that depends on it.
	std::map<FilePath, std::vector<std::string>> commandHashes;
	for (const std::shared_ptr<IndexerCommand>& command: getIndexerCommands(info))
	{
		std::string hash = command->getIndexerCommandHash();
		if (!hash.empty())
		{
			commandHashes[command->getSourceFilePath()].push_back(std::move(hash));
		}
	}

	// Consumed by iteration only (never point-looked-up), so a flat vector.
	std::vector<std::pair<FilePath, std::string>> hashes;
	hashes.reserve(commandHashes.size());
	for (auto& [path, fileHashes]: commandHashes)
	{
		std::ranges::sort(fileHashes);
		std::string hash = fileHashes.front();
		for (std::size_t i = 1; i < fileHashes.size(); ++i)
		{
			hash += "+" + fileHashes[i];
		}
		hashes.emplace_back(path, std::move(hash));
	}
	return hashes;
}

std::vector<std::pair<FilePath, std::string>> SourceGroup::getAllSourceFileCommandHashes() const
{
	RefreshInfo info;
	info.filesToIndex = getAllSourceFilePaths();
	info.mode = RefreshMode::ALL_FILES;
	// Only the commands' hashes are wanted. Module flags (the prebuilt-module path, `-x c++-module`)
	// are left out of them that way, which is right: they follow from the sources, whose changes the
	// refresh detects by themselves, not from the configuration the hash watches.
	info.buildPrerequisites = false;
	return getSourceFileCommandHashes(info);
}

std::function<void()> SourceGroup::getPreIndexWork(
	std::shared_ptr<StorageProvider>  /*storageProvider*/, std::shared_ptr<DialogView>  /*dialogView*/) const
{
	return {};
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
