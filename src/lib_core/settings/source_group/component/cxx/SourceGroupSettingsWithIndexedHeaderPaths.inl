// Inline implementations for SourceGroupSettingsWithIndexedHeaderPaths.h (included at its end). All definitions inline: the family
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

inline std::vector<FilePath> SourceGroupSettingsWithIndexedHeaderPaths::getIndexedHeaderPaths() const
{
	return m_indexedHeaderPaths;
}

inline std::vector<FilePath> SourceGroupSettingsWithIndexedHeaderPaths::getIndexedHeaderPathsExpandedAndAbsolute() const
{
	return getProjectSettings()->makePathsExpandedAndAbsolute(getIndexedHeaderPaths());
}

inline void SourceGroupSettingsWithIndexedHeaderPaths::setIndexedHeaderPaths(
	const std::vector<FilePath>& indexedHeaderPaths)
{
	m_indexedHeaderPaths = indexedHeaderPaths;
}

inline bool SourceGroupSettingsWithIndexedHeaderPaths::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithIndexedHeaderPaths* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithIndexedHeaderPaths*>(other);

	return (otherPtr && utility::isPermutation(m_indexedHeaderPaths, otherPtr->m_indexedHeaderPaths));
}

inline void SourceGroupSettingsWithIndexedHeaderPaths::load(const ConfigManager* config, const std::string& key)
{
	setIndexedHeaderPaths(config->getValuesOrDefaults(
		key + "/indexed_header_paths/indexed_header_path", std::vector<FilePath>()));
}

inline void SourceGroupSettingsWithIndexedHeaderPaths::save(ConfigManager* config, const std::string& key)
{
	config->setValues(key + "/indexed_header_paths/indexed_header_path", getIndexedHeaderPaths());
}
