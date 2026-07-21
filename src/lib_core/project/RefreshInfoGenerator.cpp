#include "RefreshInfoGenerator.h"

#ifndef SRCTRL_MODULE_BUILD
#include "FileInfo.h"
#include "FileSystem.h"
#endif
#include "PersistentStorage.h"
#include "RefreshInfo.h"
#include "SourceGroup.h"
#include "SourceGroupStatusType.h"
#ifndef SRCTRL_MODULE_BUILD
#include "TextAccess.h"
#include "utility.h"
#endif

#include <unordered_map>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.utility;
#endif

RefreshInfo RefreshInfoGenerator::getRefreshInfoForUpdatedFiles(
	const std::vector<std::shared_ptr<SourceGroup>>& sourceGroups,
	std::shared_ptr<const PersistentStorage> storage)
{
	// 1) Divide filepaths that are already known by the storage to "unchanged and indexed",
	// "unchanged and non-indexed" and "changed"
	std::set<FilePath> unchangedIndexedFilePaths;
	std::set<FilePath> unchangedNonindexedFilePaths;
	std::set<FilePath> changedFilePaths;
	std::set<FilePath> removedIndexedFilePaths;

	// The source files the project would index right now. Used both to (re-)index a known
	// source that is not yet indexed and, in step 2.3, to protect referenced headers.
	const std::set<FilePath> allSourceFilePathsFromSourcegroups = getAllSourceFilePaths(sourceGroups);

	{
		const std::vector<FileInfo> fileInfosFromStorage = storage->getFileInfoForAllFiles();

		// Files left incomplete by an interrupted index (complete = 0) must be
		// re-indexed even though their content is unchanged.
		const std::set<FilePath> incompleteFilePaths = storage->getIncompleteFiles();

		// Files still claimed by an enabled source group (as a source or a contained
		// header). A previously indexed file that is no longer claimed by any group has
		// been removed from the project and must be cleared (step 2.4) -- unless it is
		// still referenced by an unchanged in-scope source (e.g. a precompiled or shared
		// header kept for its includers), which the reference check in step 2.3 spares.
		std::set<FilePath> containedFilePaths;
		{
			std::set<FilePath> storedFilePaths;
			for (const FileInfo& info: fileInfosFromStorage)
			{
				storedFilePaths.insert(info.path);
			}
			for (const std::shared_ptr<SourceGroup>& sourceGroup: sourceGroups)
			{
				if (sourceGroup->getStatus() == SourceGroupStatusType::ENABLED)
				{
					utility::append(
						containedFilePaths, sourceGroup->filterToContainedFilePaths(storedFilePaths));
				}
			}
		}

		// A stored file needs (re-)indexing only when it no longer exists (removed from
		// disk), its content actually changed, or a previous index left it incomplete.
		// Otherwise it is unchanged and kept -- whether it is a source-group source or a
		// header (e.g. a precompiled header) that is indexed only transitively via the
		// files that include it. Treating a present, unchanged header as changed here
		// would cascade through getReferencing() below and re-index every file that
		// includes it on every incremental refresh (a header is indexed=0 and stores no
		// content, and a precompiled header is indexed=1 but is not enumerated as a
		// source -- both previously fell through to the "changed" branches).
		for (const FileInfo& info: fileInfosFromStorage)
		{
			const bool changed = !info.path.exists() ||
				incompleteFilePaths.find(info.path) != incompleteFilePaths.end() ||
				didFileChange(info, storage);
			if (changed)
			{
				changedFilePaths.insert(info.path);
			}
			else if (storage->getFilePathIndexed(info.path))
			{
				if (containedFilePaths.find(info.path) != containedFilePaths.end())
				{
					unchangedIndexedFilePaths.insert(info.path);
				}
				else
				{
					// Indexed but no longer part of the project -> a removal candidate.
					removedIndexedFilePaths.insert(info.path);
				}
			}
			else if (allSourceFilePathsFromSourcegroups.find(info.path) !=
					 allSourceFilePathsFromSourcegroups.end())
			{
				// A known source that is not yet indexed will be indexed now; route it
				// through the "changed" path so its stale non-indexed record is cleared
				// first. (A source is a leaf translation unit, so this does not cascade.)
				changedFilePaths.insert(info.path);
			}
			else
			{
				// A non-indexed header (indexed on demand via its includers) or a file no
				// longer part of the project -- keep the record as-is.
				unchangedNonindexedFilePaths.insert(info.path);
			}
		}
	}

	// 1b) Flag-aware refresh: a compile-command change (new define/include/std)
	// alters a translation unit without touching its source mtime, so the mtime/
	// content check above misses it. Compare the stored per-file command hash with
	// the current one and treat a mismatch exactly like a content change. Files
	// without a stored hash (older databases, never-indexed files) are skipped, so
	// upgrading never triggers a spurious full re-index.
	{
		const std::unordered_map<std::string, std::string> storedHashes = storage->getFileCommandHashes();
		if (!storedHashes.empty())
		{
			for (const std::shared_ptr<SourceGroup>& sourceGroup: sourceGroups)
			{
				if (sourceGroup->getStatus() != SourceGroupStatusType::ENABLED)
				{
					continue;
				}
				for (const auto& [path, currentHash]: sourceGroup->getAllSourceFileCommandHashes())
				{
					const auto it = storedHashes.find(path.str());
					if (it != storedHashes.end() && it->second != currentHash && path.exists())
					{
						changedFilePaths.insert(path);
					}
				}
			}
		}
	}

	// 2) Figure out which files need to be cleared
	// 2.1) Add all changed files
	std::set<FilePath> filesToClear = changedFilePaths;

	// 2.2) Add files that are reference the changed files
	utility::append(filesToClear, storage->getReferencing(changedFilePaths));

	// 2.3) Handle files that are referenced by the files that will be cleared. These will be
	// re-indexed on the fly. However, we do not
	//		need to clear files that are also referenced by unchanged source files, because
	//otherwise we will lose these connections.
	// 2.3.1) Get all source file paths that will not be cleared.
	// - Initially this list contains all source file paths the project would index right now.
	// - Then we remove all source files that will be cleared
	// - NOTE: Source files that are new to the project will part of this list, but won't result in
	// any referenced
	//   paths because they are not part of the DB. Source files that are new to the project but are
	//   already in the DB will be removed from this list if they have changed or reference changed
	//   files.
	std::set<FilePath> staticSourceFiles = allSourceFilePathsFromSourcegroups;
	for (const FilePath& path: filesToClear)
	{
		staticSourceFiles.erase(path);
	}

	// 2.3.2) Get the files referenced by the sources that will NOT be cleared.
	const std::set<FilePath> staticReferencedFilePaths = storage->getReferenced(staticSourceFiles);

	// 2.4) Clear files that were indexed but are no longer part of the project, unless
	// they are still referenced by an unchanged in-scope source. A precompiled or shared
	// header is not enumerated as a source, so it counts as "removed" here, but its
	// includers still need it -- clearing it would drop their symbols until a full
	// re-index. staticReferencedFilePaths holds exactly the files kept alive that way.
	// Done before 2.3.3 so a removed file's own referenced-only headers are cleared with it.
	for (const FilePath& path: removedIndexedFilePaths)
	{
		if (staticReferencedFilePaths.find(path) == staticReferencedFilePaths.end())
		{
			filesToClear.insert(path);
		}
	}

	// 2.3.3) Add "dynamicReferencedFilePaths" to "filesToClear" that are not referenced by static
	// paths, because these files may not be
	//        referenced anymore. If they still are, they will be re-added when encountered during
	//        re-indexing.
	const std::set<FilePath> dynamicReferencedFilePaths = storage->getReferenced(filesToClear);
	for (const FilePath& path: dynamicReferencedFilePaths)
	{
		if (staticReferencedFilePaths.find(path) == staticReferencedFilePaths.end() &&
			staticSourceFiles.find(path) == staticSourceFiles.end())
		{
			filesToClear.insert(path);
		}
	}

	// 3) Figure out which files need to be indexed
	std::set<FilePath> filesToIndex;
	for (const FilePath& path: allSourceFilePathsFromSourcegroups)
	{
		if (filesToClear.find(path) != filesToClear.end() ||	// file will be cleared
			unchangedIndexedFilePaths.find(path) ==
				unchangedIndexedFilePaths.end())	// file has been changed or added
		{
			filesToIndex.insert(path);
		}
	}

	// 4) Store and return this information
	RefreshInfo info;
	info.mode = RefreshMode::UPDATED_FILES;
	info.filesToIndex = filesToIndex;
	for (const FilePath &fileToClear: filesToClear)
	{
		if (storage->getFilePathIndexed(fileToClear))
		{
			info.filesToClear.insert(fileToClear);
		}
		else
		{
			info.nonIndexedFilesToClear.insert(fileToClear);
		}
	}

	return info;
}

RefreshInfo RefreshInfoGenerator::getRefreshInfoForIncompleteFiles(
	const std::vector<std::shared_ptr<SourceGroup>>& sourceGroups,
	std::shared_ptr<const PersistentStorage> storage)
{
	RefreshInfo info = getRefreshInfoForUpdatedFiles(sourceGroups, storage);
	info.mode = RefreshMode::UPDATED_AND_INCOMPLETE_FILES;

	std::set<FilePath> incompleteFiles;
	{
		const std::set<FilePath> filesToClear = utility::concat(
			info.filesToClear, info.nonIndexedFilesToClear);
		for (const FilePath& path: storage->getIncompleteFiles())
		{
			if (filesToClear.find(path) == filesToClear.end())
			{
				incompleteFiles.insert(path);
			}
		}
	}

	if (!incompleteFiles.empty())
	{
		utility::append(incompleteFiles, storage->getReferencing(incompleteFiles));

		std::set<FilePath> staticSourceFilePaths = getAllSourceFilePaths(sourceGroups);
		for (const FilePath& path: incompleteFiles)
		{
			staticSourceFilePaths.erase(path);

			if (storage->getFilePathIndexed(path))
			{
				info.filesToClear.insert(path);
			}
			else
			{
				info.nonIndexedFilesToClear.insert(path);
			}
		}

		for (const std::shared_ptr<SourceGroup>& sourceGroup: sourceGroups)
		{
			if (sourceGroup->getStatus() == SourceGroupStatusType::ENABLED)
			{
				utility::append(
					info.filesToIndex,
					sourceGroup->filterToContainedSourceFilePath(staticSourceFilePaths));
			}
		}
	}

	return info;
}

RefreshInfo RefreshInfoGenerator::getRefreshInfoForAllFiles(
	const std::vector<std::shared_ptr<SourceGroup>>& sourceGroups)
{
	RefreshInfo info;
	info.mode = RefreshMode::ALL_FILES;
	info.filesToIndex = getAllSourceFilePaths(sourceGroups);
	return info;
}

std::set<FilePath> RefreshInfoGenerator::getAllSourceFilePaths(
	const std::vector<std::shared_ptr<SourceGroup>>& sourceGroups)
{
	std::set<FilePath> allSourceFilePaths;

	for (const std::shared_ptr<SourceGroup>& sourceGroup: sourceGroups)
	{
		if (sourceGroup->getStatus() == SourceGroupStatusType::ENABLED)
		{
			for (const FilePath& sourceFilePath: sourceGroup->getAllSourceFilePaths())
			{
				if (sourceFilePath.exists())
				{
					allSourceFilePaths.insert(sourceFilePath);
				}
			}
		}
	}

	return allSourceFilePaths;
}

bool RefreshInfoGenerator::didFileChange(
	const FileInfo& info, std::shared_ptr<const PersistentStorage> storage)
{
	FileInfo diskFileInfo = FileSystem::getFileInfoForPath(info.path);
	// The stored mtime is second-precision (persisted via TimeStamp::toString), while
	// the disk mtime carries sub-seconds. Compare at the stored precision, otherwise a
	// sub-second difference on an otherwise-unchanged file reads as newer -- and for a
	// content-less file (a header, whose content is not stored) the branch below then
	// assumes it changed, cascading a re-index through every file that includes it.
	if (TimeStamp(diskFileInfo.lastWriteTime.toString()) > info.lastWriteTime)
	{
		if (!storage->hasContentForFile(info.path))
		{
			return true;
		}

		std::shared_ptr<TextAccess> storedFileContent = storage->getFileContent(info.path, false);
		std::shared_ptr<TextAccess> diskFileContent = TextAccess::createFromFile(diskFileInfo.path);

		const std::vector<std::string>& diskFileLines = diskFileContent->getAllLines();
		const std::vector<std::string>& storedFileLines = storedFileContent->getAllLines();

		if (diskFileLines.size() != storedFileLines.size())
		{
			return true;
		}

		for (size_t i = 0; i < diskFileLines.size(); i++)
		{
			if (diskFileLines[i] != storedFileLines[i])
			{
				return true;
			}
		}
		return false;
	}
	return false;
}
