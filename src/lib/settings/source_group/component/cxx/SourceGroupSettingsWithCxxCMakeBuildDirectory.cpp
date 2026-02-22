#include "SourceGroupSettingsWithCxxCMakeBuildDirectory.h"

#include "CMakeFileAPIReader.h"
#include "ProjectSettings.h"
#include "utilityFile.h"

FilePath SourceGroupSettingsWithCxxCMakeBuildDirectory::getSourceDirectory() const
{
	return m_sourceDirectory;
}

FilePath SourceGroupSettingsWithCxxCMakeBuildDirectory::getSourceDirectoryExpandedAndAbsolute() const
{
	return utility::getExpandedAndAbsolutePath(
		getSourceDirectory(), getProjectSettings()->getProjectDirectoryPath());
}

void SourceGroupSettingsWithCxxCMakeBuildDirectory::setSourceDirectory(const FilePath& path)
{
	m_sourceDirectory = path;
}

const std::string& SourceGroupSettingsWithCxxCMakeBuildDirectory::getPresetName() const
{
	return m_presetName;
}

void SourceGroupSettingsWithCxxCMakeBuildDirectory::setPresetName(const std::string& name)
{
	m_presetName = name;
}

FilePath SourceGroupSettingsWithCxxCMakeBuildDirectory::resolveBuildDirectory() const
{
	return CMakeFileAPIReader::resolveBinaryDir(
		getSourceDirectoryExpandedAndAbsolute(), m_presetName);
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
	return o && m_sourceDirectory == o->m_sourceDirectory && m_presetName == o->m_presetName &&
		m_targetGlob == o->m_targetGlob && m_configuration == o->m_configuration;
}

void SourceGroupSettingsWithCxxCMakeBuildDirectory::load(
	const ConfigManager* config, const std::string& key)
{
	setSourceDirectory(
		FilePath(config->getValueOrDefault(key + "/cmake/source_directory", std::string{})));
	m_presetName = config->getValueOrDefault(key + "/cmake/preset_name", std::string{});
	m_targetGlob = config->getValueOrDefault(key + "/cmake/target_glob", std::string{});
	m_configuration = config->getValueOrDefault(key + "/cmake/configuration", std::string{});
}

void SourceGroupSettingsWithCxxCMakeBuildDirectory::save(
	ConfigManager* config, const std::string& key)
{
	config->setValue(key + "/cmake/source_directory", getSourceDirectory().str());
	config->setValue(key + "/cmake/preset_name", m_presetName);
	config->setValue(key + "/cmake/target_glob", m_targetGlob);
	config->setValue(key + "/cmake/configuration", m_configuration);
}
