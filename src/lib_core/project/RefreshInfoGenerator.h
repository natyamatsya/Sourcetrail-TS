#ifndef REFRESH_INFO_GENERATOR_H
#define REFRESH_INFO_GENERATOR_H

#include <memory>
#include <set>
#include <vector>

struct FileInfo;
class FilePath;
class PersistentStorage;
struct RefreshInfo;
class SourceGroup;

class RefreshInfoGenerator
{
public:
	static RefreshInfo getRefreshInfoForUpdatedFiles(
		const std::vector<std::shared_ptr<SourceGroup>>& sourceGroups,
		std::shared_ptr<const PersistentStorage> storage);

	static RefreshInfo getRefreshInfoForIncompleteFiles(
		const std::vector<std::shared_ptr<SourceGroup>>& sourceGroups,
		std::shared_ptr<const PersistentStorage> storage);

	static RefreshInfo getRefreshInfoForAllFiles(
		const std::vector<std::shared_ptr<SourceGroup>>& sourceGroups);

	//! The indexed files that changed on disk since they were indexed, by the rule an
	//! update uses (removed, or newer with other content): what the status bar's
	//! "N files changed" counts and a code file's title marks with "*".
	static std::set<FilePath> getChangedFilePaths(std::shared_ptr<const PersistentStorage> storage);

private:
	static std::set<FilePath> getAllSourceFilePaths(
		const std::vector<std::shared_ptr<SourceGroup>>& sourceGroups);

	static bool didFileChange(const FileInfo& info, std::shared_ptr<const PersistentStorage> storage);
};

#endif	  // REFRESH_INFO_GENERATOR_H
