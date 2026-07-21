// Inline implementations for SourceGroupSettingsWithCxxCMakeBuildDirectory.h (included at its
// end). All definitions inline EXCEPT resolveBuildDirectory, which stays in the classic .cpp: it
// needs CMakeFileAPIReader, whose textual closure is GMF-poison for srctrl.settings, and nothing
// module-side references it (classic-callers-only, the include-only-member contract). The
// virtuals MUST be here -- the wrapper emits the concrete types' vtables, and vtable references
// to module-attached members are module-mangled.

#pragma once

// Family-internal include, unguarded (same module either way).
#include "ProjectSettings.h"

// Cross-module deps: the wrapper supplies these via imports.
#ifndef SRCTRL_MODULE_PURVIEW
#include "ConfigManager.h"
#include "utilityFile.h"
#endif

inline FilePath SourceGroupSettingsWithCxxCMakeBuildDirectory::getSourceDirectory() const
{
	return m_sourceDirectory;
}

inline FilePath SourceGroupSettingsWithCxxCMakeBuildDirectory::getSourceDirectoryExpandedAndAbsolute() const
{
	return utility::getExpandedAndAbsolutePath(
		getSourceDirectory(), getProjectSettings()->getProjectDirectoryPath());
}

inline void SourceGroupSettingsWithCxxCMakeBuildDirectory::setSourceDirectory(const FilePath& path)
{
	m_sourceDirectory = path;
}

inline const std::string& SourceGroupSettingsWithCxxCMakeBuildDirectory::getPresetName() const
{
	return m_presetName;
}

inline void SourceGroupSettingsWithCxxCMakeBuildDirectory::setPresetName(const std::string& name)
{
	m_presetName = name;
}

inline const std::string& SourceGroupSettingsWithCxxCMakeBuildDirectory::getTargetGlob() const
{
	return m_targetGlob;
}

inline void SourceGroupSettingsWithCxxCMakeBuildDirectory::setTargetGlob(const std::string& glob)
{
	m_targetGlob = glob;
}

inline const std::string& SourceGroupSettingsWithCxxCMakeBuildDirectory::getConfiguration() const
{
	return m_configuration;
}

inline void SourceGroupSettingsWithCxxCMakeBuildDirectory::setConfiguration(const std::string& config)
{
	m_configuration = config;
}

inline bool SourceGroupSettingsWithCxxCMakeBuildDirectory::equals(const SourceGroupSettingsBase* other) const
{
	const auto* o = dynamic_cast<const SourceGroupSettingsWithCxxCMakeBuildDirectory*>(other);
	return o && m_sourceDirectory == o->m_sourceDirectory && m_presetName == o->m_presetName &&
		m_targetGlob == o->m_targetGlob && m_configuration == o->m_configuration;
}

inline void SourceGroupSettingsWithCxxCMakeBuildDirectory::load(
	const ConfigManager* config, const std::string& key)
{
	setSourceDirectory(
		FilePath(config->getValueOrDefault(key + "/cmake/source_directory", std::string{})));
	m_presetName = config->getValueOrDefault(key + "/cmake/preset_name", std::string{});
	m_targetGlob = config->getValueOrDefault(key + "/cmake/target_glob", std::string{});
	m_configuration = config->getValueOrDefault(key + "/cmake/configuration", std::string{});
}

inline void SourceGroupSettingsWithCxxCMakeBuildDirectory::save(
	ConfigManager* config, const std::string& key)
{
	config->setValue(key + "/cmake/source_directory", getSourceDirectory().str());
	config->setValue(key + "/cmake/preset_name", m_presetName);
	config->setValue(key + "/cmake/target_glob", m_targetGlob);
	config->setValue(key + "/cmake/configuration", m_configuration);
}
