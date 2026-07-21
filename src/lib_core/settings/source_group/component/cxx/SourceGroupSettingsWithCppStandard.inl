// Inline implementations for SourceGroupSettingsWithCppStandard.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "ConfigManager.h"
#include <ToolChain.h>
#endif

inline std::string SourceGroupSettingsWithCppStandard::getDefaultCppStandard()
{
	return ClangCompiler::getLatestCppStandard();
}

inline std::vector<std::string> SourceGroupSettingsWithCppStandard::getAvailableCppStandards()
{
	return ClangCompiler::getAvailableCppStandards();
}

inline std::string SourceGroupSettingsWithCppStandard::getCppStandard() const
{
	if (m_cppStandard.empty())
	{
		return getDefaultCppStandard();
	}
	return m_cppStandard;
}

inline void SourceGroupSettingsWithCppStandard::setCppStandard(const std::string& standard)
{
	m_cppStandard = standard;
}

inline bool SourceGroupSettingsWithCppStandard::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithCppStandard* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithCppStandard*>(other);

	return (otherPtr && m_cppStandard == otherPtr->m_cppStandard);
}

inline void SourceGroupSettingsWithCppStandard::load(const ConfigManager* config, const std::string& key)
{
	setCppStandard(config->getValueOrDefault<std::string>(key + "/cpp_standard", ""));
}

inline void SourceGroupSettingsWithCppStandard::save(ConfigManager* config, const std::string& key)
{
	config->setValue(key + "/cpp_standard", getCppStandard());
}
