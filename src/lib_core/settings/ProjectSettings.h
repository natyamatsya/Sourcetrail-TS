#ifndef PROJECT_SETTINGS_H
#define PROJECT_SETTINGS_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "LanguageType.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>
#include <string>
#include <vector>

#include "Settings.h"
#endif

#ifndef SRCTRL_MODULE_PURVIEW
SRCTRL_EXPORT class SourceGroupSettings;
#endif

SRCTRL_EXPORT class ProjectSettings: public Settings
{
public:
	static const std::string PROJECT_FILE_EXTENSION;
	static const std::string BOOKMARK_DB_FILE_EXTENSION;
	static const std::string INDEX_DB_FILE_EXTENSION;
	static const std::string TEMP_INDEX_DB_FILE_EXTENSION;

	static const size_t VERSION;
	static LanguageType getLanguageOfProject(const FilePath& filePath);
	static bool isProjectFilePath(const FilePath& filePath);
	static bool isTomlProjectFilePath(const FilePath& filePath);

	ProjectSettings();
	ProjectSettings(const FilePath& projectFilePath);
	~ProjectSettings() override;

	bool equalsExceptNameAndLocation(const ProjectSettings& other) const;

	bool reload();

	FilePath getProjectFilePath() const;
	void setProjectFilePath(std::string projectName, const FilePath& projectFileLocation);
	FilePath getDependenciesDirectoryPath() const;

	FilePath getDBFilePath() const;
	FilePath getTempDBFilePath() const;
	FilePath getBookmarkDBFilePath() const;

	std::string getProjectName() const;
	FilePath getProjectDirectoryPath() const;

	std::string getDescription() const;

	//! Project-declared IPC channel-name prefixes (`channel_name_prefixes`). A
	//! string-literal constant whose value starts with one of these is a channel
	//! name, and every language's indexer binds its declaration to a `channel:`
	//! contract atom. Project-wide because a channel is by definition not the
	//! property of one source group. Empty = the feature is off.
	//! See context/DESIGN_XLANG_BOUNDARIES.md.
	std::vector<std::string> getChannelNamePrefixes() const;
	void setChannelNamePrefixes(const std::vector<std::string>& prefixes);

	std::vector<std::shared_ptr<SourceGroupSettings>> getAllSourceGroupSettings() const;
	void setAllSourceGroupSettings(const std::vector<std::shared_ptr<SourceGroupSettings>>& allSettings);

	std::vector<FilePath> makePathsExpandedAndAbsolute(const std::vector<FilePath>& paths) const;
	FilePath makePathExpandedAndAbsolute(const FilePath& path) const;
};

#include "ProjectSettings.inl"

#endif	  // PROJECT_SETTINGS_H
