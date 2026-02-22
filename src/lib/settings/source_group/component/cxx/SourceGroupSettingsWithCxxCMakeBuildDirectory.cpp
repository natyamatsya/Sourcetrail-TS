#include "SourceGroupSettingsWithCxxCMakeBuildDirectory.h"

#include "ProjectSettings.h"
#include "utilityFile.h"

FilePath SourceGroupSettingsWithCxxCMakeBuildDirectory::getBuildDirectory() const
{
	return m_buildDirectory;
}

FilePath SourceGroupSettingsWithCxxCMakeBuildDirectory::getBuildDirectoryExpandedAndAbsolute() const
{
	return utility::getExpandedAndAbsolutePath(
		getBuildDirectory(), getProjectSettings()->getProjectDirectoryPath());
}

void SourceGroupSettingsWithCxxCMakeBuildDirectory::setBuildDirectory(const FilePath& path)
{
	m_buildDirectory = path;
}

const std::string& SourceGroupSettingsWithCxxCMakeBuildDirectory::getTargetGlob() const
{
	return m_targetGlob;
}

void SourceGroupSettingsWithCxxCMakeBuildDirectory::setTargetGlob(const std::string& glob)
{
	m_targetGlob = glob;
}

const std::string& SourceGroupSettingsWithCxxCMakeBuildDirectory::getConfiguration() const
{
	return m_configuration;
}

void SourceGroupSettingsWithCxxCMakeBuildDirectory::setConfiguration(const std::string& config)
{
	m_configuration = config;
}

bool SourceGroupSettingsWithCxxCMakeBuildDirectory::equals(const SourceGroupSettingsBase* other) const
{
	const auto* o = dynamic_cast<const SourceGroupSettingsWithCxxCMakeBuildDirectory*>(other);
	return o && m_buildDirectory == o->m_buildDirectory && m_targetGlob == o->m_targetGlob &&
		m_configuration == o->m_configuration;
}

void SourceGroupSettingsWithCxxCMakeBuildDirectory::load(
	const ConfigManager* config, const std::string& key)
{
	setBuildDirectory(
		FilePath(config->getValueOrDefault(key + "/cmake/build_directory", std::string{})));
	m_targetGlob = config->getValueOrDefault(key + "/cmake/target_glob", std::string{});
	m_configuration = config->getValueOrDefault(key + "/cmake/configuration", std::string{});
}

void SourceGroupSettingsWithCxxCMakeBuildDirectory::save(
	ConfigManager* config, const std::string& key)
{
	config->setValue(key + "/cmake/build_directory", getBuildDirectory().str());
	config->setValue(key + "/cmake/target_glob", m_targetGlob);
	config->setValue(key + "/cmake/configuration", m_configuration);
}
