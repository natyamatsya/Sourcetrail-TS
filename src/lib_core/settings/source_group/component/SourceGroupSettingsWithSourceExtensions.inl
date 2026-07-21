// Inline implementations for SourceGroupSettingsWithSourceExtensions.h (included at its end). All definitions inline: the family
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

inline std::vector<std::string> SourceGroupSettingsWithSourceExtensions::getSourceExtensions() const
{
	if (m_sourceExtensions.empty())
	{
		return getDefaultSourceExtensions();
	}
	return m_sourceExtensions;
}

inline void SourceGroupSettingsWithSourceExtensions::setSourceExtensions(
	const std::vector<std::string>& sourceExtensions)
{
	m_sourceExtensions = sourceExtensions;
}

inline bool SourceGroupSettingsWithSourceExtensions::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithSourceExtensions* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithSourceExtensions*>(other);

	return (otherPtr && utility::isPermutation(m_sourceExtensions, otherPtr->m_sourceExtensions));
}

inline void SourceGroupSettingsWithSourceExtensions::load(const ConfigManager* config, const std::string& key)
{
	setSourceExtensions(config->getValuesOrDefaults(
		key + "/source_extensions/source_extension", std::vector<std::string>()));
}

inline void SourceGroupSettingsWithSourceExtensions::save(ConfigManager* config, const std::string& key)
{
	config->setValues(key + "/source_extensions/source_extension", getSourceExtensions());
}
