#ifndef SOURCE_GROUP_SETTINGS_CXX_CMAKE_FILE_API_H
#define SOURCE_GROUP_SETTINGS_CXX_CMAKE_FILE_API_H

#include "SourceGroupSettingsWithComponents.h"
#include "SourceGroupSettingsWithCxxPathsAndFlags.h"
#include "SourceGroupSettingsWithExcludeFilters.h"
#include "SourceGroupSettingsWithIndexedHeaderPaths.h"

// Settings for a C/C++ source group driven by the CMake File-based API.
// The user supplies a CMake build directory; Sourcetrail writes a query file
// and reads the structured reply to obtain per-file compile flags.
class SourceGroupSettingsCxxCMakeFileAPI
	: public SourceGroupSettingsWithComponents<
		  SourceGroupSettingsWithCxxPathsAndFlags,
		  SourceGroupSettingsWithExcludeFilters,
		  SourceGroupSettingsWithIndexedHeaderPaths>
{
public:
	SourceGroupSettingsCxxCMakeFileAPI(
		const std::string& id, const ProjectSettings* projectSettings)
		: SourceGroupSettingsWithComponents(
			  SourceGroupType::CXX_CMAKE_FILE_API, id, projectSettings)
	{
	}

	std::shared_ptr<SourceGroupSettings> createCopy() const override
	{
		return std::make_shared<SourceGroupSettingsCxxCMakeFileAPI>(*this);
	}

	// Path to the CMake build directory (contains CMakeCache.txt).
	FilePath getBuildDirectory() const
	{
		return getPathRelativeToProjectFileLocation(m_buildDirectory);
	}

	FilePath getBuildDirectoryExpandedAndAbsolute() const
	{
		return FilePath(m_buildDirectory).makeAbsolute();
	}

	void setBuildDirectory(const FilePath& path)
	{
		m_buildDirectory = path;
	}

	// Optional: restrict indexing to targets matching this glob (e.g. "MyLib*").
	// Empty string means all targets.
	const std::string& getTargetGlob() const
	{
		return m_targetGlob;
	}

	void setTargetGlob(const std::string& glob)
	{
		m_targetGlob = glob;
	}

	// Optional: CMake configuration name (e.g. "Debug", "Release").
	// Empty string means the first available configuration.
	const std::string& getConfiguration() const
	{
		return m_configuration;
	}

	void setConfiguration(const std::string& config)
	{
		m_configuration = config;
	}

	void load(const ConfigManager* config, const std::string& key) override
	{
		SourceGroupSettingsWithComponents::load(config, key);
		m_buildDirectory = FilePath(config->getValueOrDefault(key + "/build_directory", std::string{}));
		m_targetGlob = config->getValueOrDefault(key + "/target_glob", std::string{});
		m_configuration = config->getValueOrDefault(key + "/configuration", std::string{});
	}

	void save(ConfigManager* config, const std::string& key) override
	{
		SourceGroupSettingsWithComponents::save(config, key);
		config->setValue(key + "/build_directory", m_buildDirectory.str());
		config->setValue(key + "/target_glob", m_targetGlob);
		config->setValue(key + "/configuration", m_configuration);
	}

	bool equals(const SourceGroupSettings* other) const override
	{
		const auto* o = dynamic_cast<const SourceGroupSettingsCxxCMakeFileAPI*>(other);
		return o && SourceGroupSettingsWithComponents::equals(other) &&
			m_buildDirectory == o->m_buildDirectory && m_targetGlob == o->m_targetGlob &&
			m_configuration == o->m_configuration;
	}

private:
	FilePath m_buildDirectory;
	std::string m_targetGlob;
	std::string m_configuration;
};

#endif	  // SOURCE_GROUP_SETTINGS_CXX_CMAKE_FILE_API_H
