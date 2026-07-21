// Inline implementations for SourceGroupSettingsWithCxxPchOptions.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Family-internal includes stay unguarded: same module either way; include guards
// + inl-after-class ordering make the cross-references resolve in both builds.
#include "ProjectSettings.h"

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "utility.h"
#include "utilityFile.h"
#endif

inline FilePath SourceGroupSettingsWithCxxPchOptions::getPchDependenciesDirectoryPath() const
{
	return getSourceGroupDependenciesDirectoryPath().concatenate("pch");
}

inline FilePath SourceGroupSettingsWithCxxPchOptions::getPchInputFilePath() const
{
	return m_pchInputFilePath;
}

inline FilePath SourceGroupSettingsWithCxxPchOptions::getPchInputFilePathExpandedAndAbsolute() const
{
	return utility::getExpandedAndAbsolutePath(
		getPchInputFilePath(), getProjectSettings()->getProjectDirectoryPath());
}

inline void SourceGroupSettingsWithCxxPchOptions::setPchInputFilePathFilePath(const FilePath& path)
{
	m_pchInputFilePath = path;
}

inline bool SourceGroupSettingsWithCxxPchOptions::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithCxxPchOptions* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithCxxPchOptions*>(other);

	return (
		otherPtr && m_pchInputFilePath == otherPtr->m_pchInputFilePath &&
		utility::isPermutation(m_pchFlags, otherPtr->m_pchFlags) &&
		m_useCompilerFlags == otherPtr->m_useCompilerFlags);
}

inline void SourceGroupSettingsWithCxxPchOptions::load(const ConfigManager* config, const std::string& key)
{
	setPchInputFilePathFilePath(
		config->getValueOrDefault(key + "/pch_input_file_path", FilePath("")));
	setPchFlags(config->getValuesOrDefaults(key + "/pch_flags/pch_flag", std::vector<std::string>()));
	setUseCompilerFlags(config->getValueOrDefault(key + "/pch_flags/use_compiler_flags", false));
}

inline void SourceGroupSettingsWithCxxPchOptions::save(ConfigManager* config, const std::string& key)
{
	config->setValue(key + "/pch_input_file_path", getPchInputFilePath().str());
	config->setValues(key + "/pch_flags/pch_flag", getPchFlags());
	config->setValue(key + "/pch_flags/use_compiler_flags", getUseCompilerFlags());
}

inline bool SourceGroupSettingsWithCxxPchOptions::getUseCompilerFlags() const
{
	return m_useCompilerFlags;
}

inline void SourceGroupSettingsWithCxxPchOptions::setUseCompilerFlags(bool useCompilerFlags)
{
	m_useCompilerFlags = useCompilerFlags;
}

inline std::vector<std::string> SourceGroupSettingsWithCxxPchOptions::getPchFlags() const
{
	return m_pchFlags;
}

inline void SourceGroupSettingsWithCxxPchOptions::setPchFlags(const std::vector<std::string>& pchFlags)
{
	m_pchFlags = pchFlags;
}
