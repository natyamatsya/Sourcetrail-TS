// Inline implementations for SourceGroupSettingsWithCStandard.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "ConfigManager.h"
#include "ToolChain.h"
#endif

inline std::string SourceGroupSettingsWithCStandard::getDefaultCStandard()
{
	return ClangCompiler::getLatestCStandard();
}

inline std::vector<std::string> SourceGroupSettingsWithCStandard::getAvailableCStandards()
{
	return ClangCompiler::getAvailableCStandards();
}

inline std::string SourceGroupSettingsWithCStandard::getCStandard() const
{
	if (m_cStandard.empty())
	{
		return getDefaultCStandard();
	}
	return m_cStandard;
}

inline void SourceGroupSettingsWithCStandard::setCStandard(const std::string& standard)
{
	m_cStandard = standard;
}


inline bool SourceGroupSettingsWithCStandard::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithCStandard* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithCStandard*>(other);

	return (otherPtr && m_cStandard == otherPtr->m_cStandard);
}

inline void SourceGroupSettingsWithCStandard::load(const ConfigManager* config, const std::string& key)
{
	setCStandard(config->getValueOrDefault<std::string>(key + "/c_standard", ""));
}

inline void SourceGroupSettingsWithCStandard::save(ConfigManager* config, const std::string& key)
{
	config->setValue(key + "/c_standard", getCStandard());
}
