#include "ProjectSettings.h"

#include "ConfigManager.h"
#include "FileSystem.h"
#include "TextAccess.h"
#include "language_package_flags.h"
#include "SourceGroupSettingsCustomCommand.h"
#include "SourceGroupSettingsUnloadable.h"
#include "logging.h"
#include "utilityFile.h"
#include "utilityString.h"
#include "utilityUuid.h"

#include <filesystem>

#include "SourceGroupSettingsCEmpty.h"
#include "SourceGroupSettingsCppEmpty.h"
#include "SourceGroupSettingsCxxCdb.h"
#include "SourceGroupSettingsCxxCMakeFileAPI.h"
#include "SourceGroupSettingsRustEmpty.h"
#include "SourceGroupSettingsSwiftEmpty.h"

namespace
{
bool hasTomlProjectExtension(const std::filesystem::path& path)
{
	if (path.extension() != ".toml")
		return false;

	return path.stem().extension() == ".srctrl";
}

template <bool Enabled, typename T>
std::shared_ptr<SourceGroupSettings> makeIfEnabled(const std::string& id, const ProjectSettings* owner)
{
	if constexpr (Enabled)
		return std::make_shared<T>(id, owner);
	else
		return std::make_shared<SourceGroupSettingsUnloadable>(id, owner);
}
}	 // namespace

const std::string ProjectSettings::PROJECT_FILE_EXTENSION = ".srctrl.toml";
const std::string ProjectSettings::LEGACY_PROJECT_FILE_EXTENSION = ".srctrlprj";
const std::string ProjectSettings::BOOKMARK_DB_FILE_EXTENSION = ".srctrl.bm";
const std::string ProjectSettings::INDEX_DB_FILE_EXTENSION = ".srctrl.db";
const std::string ProjectSettings::TEMP_INDEX_DB_FILE_EXTENSION = ".srctrl.db_tmp";

const size_t ProjectSettings::VERSION = 8;

LanguageType ProjectSettings::getLanguageOfProject(const FilePath& filePath)
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

bool ProjectSettings::isProjectFilePath(const FilePath& filePath)
{
	// Legacy XML .srctrlprj paths stay accepted at the gate: they are the entry
	// point of the one-time migration below, which converts them to .srctrl.toml
	// before anything operates on the file.
	return isTomlProjectFilePath(filePath) ||
		filePath.getPath().extension() == LEGACY_PROJECT_FILE_EXTENSION;
}

FilePath ProjectSettings::migrateLegacyProjectFile(const FilePath& projectFilePath)
{
	if (projectFilePath.getPath().extension() != LEGACY_PROJECT_FILE_EXTENSION)
	{
		return projectFilePath;
	}

	const FilePath tomlPath = projectFilePath.replaceExtension(PROJECT_FILE_EXTENSION);

	if (!projectFilePath.exists())
	{
		// Stale reference (recent-projects entry, script): the file was migrated
		// earlier — follow the sibling.
		return tomlPath.exists() ? tomlPath : projectFilePath;
	}

	// While the legacy file exists it is the source of truth: regenerate the
	// TOML from it even when a sibling is already present (earlier generator
	// versions wrote lossy TOML), then retire the legacy file so the migration
	// runs exactly once.
	std::shared_ptr<ConfigManager> config = ConfigManager::createEmpty();
	if (!config->load(TextAccess::createFromFile(projectFilePath)))
	{
		LOG_ERROR("Cannot migrate legacy project file (parse failed): " + projectFilePath.str());
		return projectFilePath;
	}

	if (!config->saveToml(tomlPath.str()))
	{
		LOG_ERROR("Cannot migrate legacy project file (write failed): " + tomlPath.str());
		return projectFilePath;
	}

	FileSystem::remove(projectFilePath);
	LOG_INFO(
		"Migrated legacy project file " + projectFilePath.str() + " to " + tomlPath.str());

	return tomlPath;
}

bool ProjectSettings::isTomlProjectFilePath(const FilePath& filePath)
{
	return hasTomlProjectExtension(filePath.getPath());
}

ProjectSettings::ProjectSettings() = default;

ProjectSettings::ProjectSettings(const FilePath& projectFilePath)
{
	setFilePath(projectFilePath);
}

ProjectSettings::~ProjectSettings() = default;

bool ProjectSettings::equalsExceptNameAndLocation(const ProjectSettings& other) const
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

bool ProjectSettings::reload()
{
	return Settings::load(getFilePath());
}

FilePath ProjectSettings::getProjectFilePath() const
{
	return getFilePath();
}

void ProjectSettings::setProjectFilePath(std::string projectName, const FilePath& projectFileLocation)
{
	setFilePath(projectFileLocation.getConcatenated("/" + projectName + PROJECT_FILE_EXTENSION));
}

FilePath ProjectSettings::getDependenciesDirectoryPath() const
{
	return getProjectDirectoryPath().concatenate("sourcetrail_dependencies");
}

static FilePath stripProjectExtension(const FilePath& path)
{
	FilePath p = path.withoutExtension();
	if (p.extension() == ".srctrl")
		p = p.withoutExtension();
	return p;
}

FilePath ProjectSettings::getDBFilePath() const
{
	return stripProjectExtension(getFilePath()).replaceExtension(INDEX_DB_FILE_EXTENSION);
}

FilePath ProjectSettings::getTempDBFilePath() const
{
	return stripProjectExtension(getFilePath()).replaceExtension(TEMP_INDEX_DB_FILE_EXTENSION);
}

FilePath ProjectSettings::getBookmarkDBFilePath() const
{
	return stripProjectExtension(getFilePath()).replaceExtension(BOOKMARK_DB_FILE_EXTENSION);
}

std::string ProjectSettings::getProjectName() const
{
	FilePath p = getFilePath().withoutExtension();
	if (p.extension() == ".srctrl")
		p = p.withoutExtension();
	return p.fileName();
}

FilePath ProjectSettings::getProjectDirectoryPath() const
{
	return getFilePath().getParentDirectory();
}

std::string ProjectSettings::getDescription() const
{
	return getValue<std::string>("description", "");
}

std::vector<std::shared_ptr<SourceGroupSettings>> ProjectSettings::getAllSourceGroupSettings() const
{
	std::vector<std::shared_ptr<SourceGroupSettings>> allSettings;
	for (const std::string& key: m_config->getSublevelKeys("source_groups"))
	{
		const std::string id = key.substr(std::string(SourceGroupSettings::s_keyPrefix).length());
		const SourceGroupType type = stringToSourceGroupType(
			getValue<std::string>(key + "/type", ""));

		std::shared_ptr<SourceGroupSettings> settings;

		switch (type)
		{
		case SourceGroupType::C_EMPTY:
			settings = makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCEmpty>(id, this);
			break;
		case SourceGroupType::CXX_EMPTY:
			settings = makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCppEmpty>(id, this);
			break;
		case SourceGroupType::CXX_CDB:
			settings = makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCxxCdb>(id, this);
			break;
		case SourceGroupType::CXX_CMAKE_FILE_API:
			settings = makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCxxCMakeFileAPI>(id, this);
			break;
		case SourceGroupType::RUST_EMPTY:
			settings = makeIfEnabled<language_packages::buildRustLanguagePackage, SourceGroupSettingsRustEmpty>(id, this);
			break;
		case SourceGroupType::SWIFT_EMPTY:
			settings = makeIfEnabled<language_packages::buildSwiftLanguagePackage, SourceGroupSettingsSwiftEmpty>(id, this);
			break;
		case SourceGroupType::CUSTOM_COMMAND:
			settings = std::make_shared<SourceGroupSettingsCustomCommand>(id, this);
			break;
		default:
			settings = std::make_shared<SourceGroupSettingsUnloadable>(id, this);
		}

		if (settings)
		{
			settings->loadSettings(m_config.get());
			allSettings.push_back(settings);
		}
		else
		{
			LOG_WARNING("Sourcegroup with id \"" + id + "\" could not be loaded.");
		}
	}

	return allSettings;
}

void ProjectSettings::setAllSourceGroupSettings(
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

std::vector<FilePath> ProjectSettings::makePathsExpandedAndAbsolute(const std::vector<FilePath>& paths) const
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

FilePath ProjectSettings::makePathExpandedAndAbsolute(const FilePath& path) const
{
	return utility::getExpandedAndAbsolutePath(path, getProjectDirectoryPath());
}
