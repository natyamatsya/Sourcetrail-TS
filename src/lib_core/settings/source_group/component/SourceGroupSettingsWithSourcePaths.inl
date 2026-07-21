// Inline implementations for SourceGroupSettingsWithSourcePaths.h (included at its end). All definitions inline: the family
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

inline std::vector<FilePath> SourceGroupSettingsWithSourcePaths::getSourcePaths() const
{
	return m_sourcePaths;
}

inline std::vector<FilePath> SourceGroupSettingsWithSourcePaths::getSourcePathsExpandedAndAbsolute() const
{
	return getProjectSettings()->makePathsExpandedAndAbsolute(getSourcePaths());
}

inline void SourceGroupSettingsWithSourcePaths::setSourcePaths(const std::vector<FilePath>& sourcePaths)
{
	m_sourcePaths = sourcePaths;
}

inline bool SourceGroupSettingsWithSourcePaths::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithSourcePaths* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithSourcePaths*>(other);

	return (otherPtr && utility::isPermutation(m_sourcePaths, otherPtr->m_sourcePaths));
}

inline void SourceGroupSettingsWithSourcePaths::load(const ConfigManager* config, const std::string& key)
{
	setSourcePaths(
		config->getValuesOrDefaults(key + "/source_paths/source_path", std::vector<FilePath>()));
}

inline void SourceGroupSettingsWithSourcePaths::save(ConfigManager* config, const std::string& key)
{
	config->setValues(key + "/source_paths/source_path", getSourcePaths());
}
