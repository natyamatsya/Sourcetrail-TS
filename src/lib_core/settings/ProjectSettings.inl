// Inline implementations for ProjectSettings.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// This inl also hosts SourceGroupSettings' three ProjectSettings-touching member bodies (keeps
// SourceGroupSettings.inl free of ProjectSettings.h). The concrete-type factory lives in
// ProjectSettingsFactory.inl -- naming the type family from ANY header the family re-enters is
// cyclic, so only the wrapper and the classic emission TU include it.

// Family-internal include, unguarded (same module either way): the bodies below walk
// SourceGroupSettings members, so the complete type is required.
#include "SourceGroupSettings.h"

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "language_package_flags.h"
#include "logging.h"
#include "utilityFile.h"
#include "utilityString.h"
#include "utilityUuid.h"
#include <filesystem>
#endif

// ODR-safe home for the helper (anonymous namespaces are an ODR trap in headers/inls).
namespace project_settings_detail
{
inline bool hasTomlProjectExtension(const std::filesystem::path& path)
{
	if (path.extension() != ".toml")
		return false;

	return path.stem().extension() == ".srctrl";
}
}	 // namespace project_settings_detail

inline const std::string ProjectSettings::PROJECT_FILE_EXTENSION = ".srctrl.toml";
inline const std::string ProjectSettings::BOOKMARK_DB_FILE_EXTENSION = ".srctrl.bm";
inline const std::string ProjectSettings::INDEX_DB_FILE_EXTENSION = ".srctrl.db";
inline const std::string ProjectSettings::TEMP_INDEX_DB_FILE_EXTENSION = ".srctrl.db.tmp";

inline const size_t ProjectSettings::VERSION = 8;

inline LanguageType ProjectSettings::getLanguageOfProject(const FilePath& filePath)
{
	LanguageType languageType = LanguageType::UNKNOWN;

	ProjectSettings projectSettings;
	projectSettings.load(filePath);
	for (const std::shared_ptr<SourceGroupSettings>& sourceGroupSettings:
		 projectSettings.getAllSourceGroupSettings())
	{
		const LanguageType currentLanguageType = getLanguageTypeForSourceGroupType(
			sourceGroupSettings->getType());
		if (languageType == LanguageType::UNKNOWN)
		{
			languageType = currentLanguageType;
		}
		else if (languageType != currentLanguageType)
		{
			// language is unknown if source groups have different languages.
			languageType = LanguageType::UNKNOWN;
			break;
		}
	}

	return languageType;
}

inline bool ProjectSettings::isProjectFilePath(const FilePath& filePath)
{
	return isTomlProjectFilePath(filePath);
}

inline bool ProjectSettings::isTomlProjectFilePath(const FilePath& filePath)
{
	return project_settings_detail::hasTomlProjectExtension(filePath.getPath());
}

inline ProjectSettings::ProjectSettings() = default;

inline ProjectSettings::ProjectSettings(const FilePath& projectFilePath)
{
	setFilePath(projectFilePath);
}

inline ProjectSettings::~ProjectSettings() = default;

inline bool ProjectSettings::equalsExceptNameAndLocation(const ProjectSettings& other) const
{
	const std::vector<std::shared_ptr<SourceGroupSettings>> allMySettings =
		getAllSourceGroupSettings();
	const std::vector<std::shared_ptr<SourceGroupSettings>> allOtherSettings =
		other.getAllSourceGroupSettings();

	if (allMySettings.size() != allOtherSettings.size())
	{
		return false;
	}

	for (const std::shared_ptr<SourceGroupSettings>& mySourceGroup: allMySettings)
	{
		bool matched = false;
		for (const std::shared_ptr<SourceGroupSettings>& otherSourceGroup: allOtherSettings)
		{
			if (mySourceGroup->equalsSettings(otherSourceGroup.get()))
			{
				matched = true;
				break;
			}
		}

		if (!matched)
		{
			return false;
		}
	}

	return true;
}

inline bool ProjectSettings::reload()
{
	return Settings::load(getFilePath());
}

inline FilePath ProjectSettings::getProjectFilePath() const
{
	return getFilePath();
}

inline void ProjectSettings::setProjectFilePath(std::string projectName, const FilePath& projectFileLocation)
{
	setFilePath(projectFileLocation.getConcatenated("/" + projectName + PROJECT_FILE_EXTENSION));
}

inline FilePath ProjectSettings::getDependenciesDirectoryPath() const
{
	return getProjectDirectoryPath().concatenate("sourcetrail_dependencies");
}

namespace project_settings_detail
{
inline FilePath stripProjectExtension(const FilePath& path)
{
	FilePath p = path.withoutExtension();
	if (p.extension() == ".srctrl")
		p = p.withoutExtension();
	return p;
}
}	 // namespace project_settings_detail

inline FilePath ProjectSettings::getDBFilePath() const
{
	return project_settings_detail::stripProjectExtension(getFilePath()).replaceExtension(INDEX_DB_FILE_EXTENSION);
}

inline FilePath ProjectSettings::getTempDBFilePath() const
{
	return project_settings_detail::stripProjectExtension(getFilePath()).replaceExtension(TEMP_INDEX_DB_FILE_EXTENSION);
}

inline FilePath ProjectSettings::getBookmarkDBFilePath() const
{
	return project_settings_detail::stripProjectExtension(getFilePath()).replaceExtension(BOOKMARK_DB_FILE_EXTENSION);
}

inline std::string ProjectSettings::getProjectName() const
{
	FilePath p = getFilePath().withoutExtension();
	if (p.extension() == ".srctrl")
		p = p.withoutExtension();
	return p.fileName();
}

inline FilePath ProjectSettings::getProjectDirectoryPath() const
{
	return getFilePath().getParentDirectory();
}

inline std::string ProjectSettings::getDescription() const
{
	return getValue<std::string>("description", "");
}

inline void ProjectSettings::setAllSourceGroupSettings(
	const std::vector<std::shared_ptr<SourceGroupSettings>>& allSettings)
{
	for (const std::string& key: m_config->getSublevelKeys("source_groups"))
	{
		m_config->removeValues(key);
	}

	for (const std::shared_ptr<SourceGroupSettings>& settings: allSettings)
	{
		const std::string key = SourceGroupSettings::s_keyPrefix + settings->getId();
		const SourceGroupType type = settings->getType();
		setValue(key + "/type", sourceGroupTypeToString(type));

		settings->saveSettings(m_config.get());
	}
}

inline std::vector<FilePath> ProjectSettings::makePathsExpandedAndAbsolute(const std::vector<FilePath>& paths) const
{
	std::vector<FilePath> p = utility::getExpandedPaths(paths);

	std::vector<FilePath> absPaths;
	const FilePath basePath = getProjectDirectoryPath();
	for (const FilePath& path: p)
	{
		if (path.isAbsolute())
		{
			absPaths.push_back(path);
		}
		else
		{
			absPaths.push_back(basePath.getConcatenated(path).makeCanonical());
		}
	}

	return absPaths;
}

inline FilePath ProjectSettings::makePathExpandedAndAbsolute(const FilePath& path) const
{
	return utility::getExpandedAndAbsolutePath(path, getProjectDirectoryPath());
}

inline FilePath SourceGroupSettings::getSourceGroupDependenciesDirectoryPath() const
{
	return getProjectSettings()->getDependenciesDirectoryPath().concatenate(getId());
}

inline FilePath SourceGroupSettings::getProjectDirectoryPath() const
{
	return m_projectSettings->getProjectDirectoryPath();
}

inline std::vector<FilePath> SourceGroupSettings::makePathsExpandedAndAbsolute(
	const std::vector<FilePath>& paths) const
{
	return m_projectSettings->makePathsExpandedAndAbsolute(paths);
}

