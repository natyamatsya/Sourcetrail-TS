// Inline implementations for SourceGroupSettingsWithCxxPathsAndFlags.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Family-internal includes stay unguarded: same module either way; include guards
// + inl-after-class ordering make the cross-references resolve in both builds.
#include "ProjectSettings.h"

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "utility.h"
#endif

inline std::vector<FilePath> SourceGroupSettingsWithCxxPathsAndFlags::getHeaderSearchPaths() const
{
	return m_headerSearchPaths;
}

inline std::vector<FilePath> SourceGroupSettingsWithCxxPathsAndFlags::getHeaderSearchPathsExpandedAndAbsolute() const
{
	return getProjectSettings()->makePathsExpandedAndAbsolute(getHeaderSearchPaths());
}

inline void SourceGroupSettingsWithCxxPathsAndFlags::setHeaderSearchPaths(
	const std::vector<FilePath>& headerSearchPaths)
{
	m_headerSearchPaths = headerSearchPaths;
}

inline std::vector<FilePath> SourceGroupSettingsWithCxxPathsAndFlags::getFrameworkSearchPaths() const
{
	return m_frameworkSearchPaths;
}

inline std::vector<FilePath> SourceGroupSettingsWithCxxPathsAndFlags::getFrameworkSearchPathsExpandedAndAbsolute() const
{
	return getProjectSettings()->makePathsExpandedAndAbsolute(getFrameworkSearchPaths());
}

inline void SourceGroupSettingsWithCxxPathsAndFlags::setFrameworkSearchPaths(
	const std::vector<FilePath>& frameworkSearchPaths)
{
	m_frameworkSearchPaths = frameworkSearchPaths;
}

inline std::vector<std::string> SourceGroupSettingsWithCxxPathsAndFlags::getCompilerFlags() const
{
	return m_compilerFlags;
}

inline void SourceGroupSettingsWithCxxPathsAndFlags::setCompilerFlags(
	const std::vector<std::string>& compilerFlags)
{
	m_compilerFlags = compilerFlags;
}

inline bool SourceGroupSettingsWithCxxPathsAndFlags::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithCxxPathsAndFlags* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithCxxPathsAndFlags*>(other);

	return (
		otherPtr && utility::isPermutation(m_headerSearchPaths, otherPtr->m_headerSearchPaths) &&
		utility::isPermutation(m_frameworkSearchPaths, otherPtr->m_frameworkSearchPaths) &&
		utility::isPermutation(m_compilerFlags, otherPtr->m_compilerFlags));
}

inline void SourceGroupSettingsWithCxxPathsAndFlags::load(const ConfigManager* config, const std::string& key)
{
	setHeaderSearchPaths(config->getValuesOrDefaults(
		key + "/header_search_paths/header_search_path", std::vector<FilePath>()));
	setFrameworkSearchPaths(config->getValuesOrDefaults(
		key + "/framework_search_paths/framework_search_path", std::vector<FilePath>()));
	setCompilerFlags(config->getValuesOrDefaults(
		key + "/compiler_flags/compiler_flag", std::vector<std::string>()));
}

inline void SourceGroupSettingsWithCxxPathsAndFlags::save(ConfigManager* config, const std::string& key)
{
	config->setValues(key + "/header_search_paths/header_search_path", getHeaderSearchPaths());
	config->setValues(
		key + "/framework_search_paths/framework_search_path", getFrameworkSearchPaths());
	config->setValues(key + "/compiler_flags/compiler_flag", getCompilerFlags());
}
