#ifndef SOURCE_GROUP_CXX_CMAKE_FILE_API_H
#define SOURCE_GROUP_CXX_CMAKE_FILE_API_H

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "CMakeFileAPIReader.h"
#include "FilePath.h"
#include "SourceGroup.h"

class DialogView;
class SourceGroupSettingsCxxCMakeFileAPI;
class StorageProvider;
class Task;

class SourceGroupCxxCMakeFileAPI : public SourceGroup
{
public:
	explicit SourceGroupCxxCMakeFileAPI(
		std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> settings);

	bool prepareIndexing() override;
	std::set<FilePath> filterToContainedFilePaths(
		const std::set<FilePath>& filePaths) const override;
	std::set<FilePath> getAllSourceFilePaths() const override;
	std::optional<BuildModelSnapshot> getBuildModelSnapshot() const override;
	std::shared_ptr<IndexerCommandProvider> getIndexerCommandProvider(
		const RefreshInfo& info) const override;
	std::vector<std::shared_ptr<IndexerCommand>> getIndexerCommands(
		const RefreshInfo& info) const override;
	//! Zero-config PCH: builds one Sourcetrail precompiled header per distinct
	//! target_precompile_headers input (+ flags) before indexing, so the heavy
	//! header is parsed once instead of per translation unit.
	std::shared_ptr<Task> getPreIndexTask(
		std::shared_ptr<StorageProvider> storageProvider,
		std::shared_ptr<DialogView> dialogView) const override;

private:
	std::shared_ptr<SourceGroupSettings> getSourceGroupSettings() override;
	std::shared_ptr<const SourceGroupSettings> getSourceGroupSettings() const override;

	std::vector<std::string> getBaseCompilerFlags() const;

	//! The full compiler flags for a source entry -- everything except the source
	//! file itself and any -include-pch -- with CMake's own PCH fragments removed.
	//! Shared by command generation and PCH generation so their flags (and hence
	//! the PCH's identity) match.
	std::vector<std::string> buildCompilerFlagsForEntry(
		const CMakeFileAPIReader::SourceEntry& entry,
		const FilePath& buildDir,
		const std::vector<std::string>& extraFlags) const;

	//! Deterministic output path for the Sourcetrail PCH of (header, flags),
	//! under the source group's dependency directory.
	FilePath pchOutputPathFor(
		const FilePath& pchHeader, const std::vector<std::string>& flags) const;

	// Cached result of resolveBuildDirectory() for the lifetime of this object.
	// Populated on first use; avoids repeated cmake -N invocations per indexing session.
	FilePath getCachedBuildDir() const;

	std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> m_settings;
	mutable FilePath m_cachedBuildDir;
};

#endif	  // SOURCE_GROUP_CXX_CMAKE_FILE_API_H
