#ifndef SOURCE_GROUP_H
#define SOURCE_GROUP_H

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "BuildModelSnapshot.h"
#include "LanguageType.h"
#include "SourceGroupStatusType.h"
#include "SourceGroupType.h"

class DialogView;
class FilePath;
class FilePathFilter;
class IndexerCommand;
class IndexerCommandProvider;
class SourceGroupSettings;
class StorageProvider;
class Task;

struct RefreshInfo;

class SourceGroup
{
public:
	virtual ~SourceGroup() = default;

	virtual bool prepareIndexing();
	virtual bool allowsPartialClearing() const;

	virtual std::set<FilePath> filterToContainedFilePaths(const std::set<FilePath>& filePaths) const = 0;
	virtual std::set<FilePath> getAllSourceFilePaths() const = 0;
	virtual std::shared_ptr<IndexerCommandProvider> getIndexerCommandProvider(
		const RefreshInfo& info) const;
	virtual std::vector<std::shared_ptr<IndexerCommand>> getIndexerCommands(
		const RefreshInfo& info) const = 0;

	//! Maps each source file in `info.filesToIndex` to a stable hash of its
	//! effective compile command. Used by the flag-aware incremental refresh to
	//! detect compile-flag changes that leave the source mtime untouched. Files
	//! whose command yields no hash (non-cxx groups) are omitted.
	std::map<FilePath, std::string> getSourceFileCommandHashes(const RefreshInfo& info) const;
	//! Same, for every source file currently in the group (the refresh side needs
	//! the full current set to compare against the stored hashes).
	std::map<FilePath, std::string> getAllSourceFileCommandHashes() const;

	virtual std::shared_ptr<Task> getPreIndexTask(
		std::shared_ptr<StorageProvider> storageProvider,
		std::shared_ptr<DialogView> dialogView) const;
	virtual std::optional<BuildModelSnapshot> getBuildModelSnapshot() const;

	SourceGroupType getType() const;
	LanguageType getLanguage() const;
	SourceGroupStatusType getStatus() const;
	std::set<FilePath> filterToContainedSourceFilePath(
		const std::set<FilePath>& staticSourceFilePaths) const;
	bool containsSourceFilePath(const FilePath& sourceFilePath) const;

protected:
	virtual std::shared_ptr<SourceGroupSettings> getSourceGroupSettings() = 0;
	virtual std::shared_ptr<const SourceGroupSettings> getSourceGroupSettings() const = 0;

	static std::set<FilePath> filterToContainedFilePaths(
		const std::set<FilePath>& filePaths,
		const std::set<FilePath>& indexedFilePaths,
		const std::set<FilePath>& indexedFileOrDirectoryPaths,
		const std::vector<FilePathFilter>& excludeFilters);
};

#endif	  // SOURCE_GROUP_H
