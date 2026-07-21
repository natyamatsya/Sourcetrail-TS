// Inline implementations for SourceGroupSettingsWithCxxCrossCompilationOptions.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Family-internal includes stay unguarded: same module either way; include guards
// + inl-after-class ordering make the cross-references resolve in both builds.
#include "ProjectSettings.h"

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "ToolChain.h"
#endif

inline bool SourceGroupSettingsWithCxxCrossCompilationOptions::getTargetOptionsEnabled() const
{
	return m_targetOptionsEnabled;
}

inline void SourceGroupSettingsWithCxxCrossCompilationOptions::setTargetOptionsEnabled(bool targetOptionsEnabled)
{
	m_targetOptionsEnabled = targetOptionsEnabled;
}

inline std::string SourceGroupSettingsWithCxxCrossCompilationOptions::getTargetArch() const
{
	return m_targetArch;
}

inline void SourceGroupSettingsWithCxxCrossCompilationOptions::setTargetArch(const std::string& arch)
{
	m_targetArch = arch;
}

inline std::string SourceGroupSettingsWithCxxCrossCompilationOptions::getTargetVendor() const
{
	return m_targetVendor;
}

inline void SourceGroupSettingsWithCxxCrossCompilationOptions::setTargetVendor(const std::string& vendor)
{
	m_targetVendor = vendor;
}

inline std::string SourceGroupSettingsWithCxxCrossCompilationOptions::getTargetSys() const
{
	return m_targetSys;
}

inline void SourceGroupSettingsWithCxxCrossCompilationOptions::setTargetSys(const std::string& sys)
{
	m_targetSys = sys;
}

inline std::string SourceGroupSettingsWithCxxCrossCompilationOptions::getTargetAbi() const
{
	return m_targetAbi;
}

inline void SourceGroupSettingsWithCxxCrossCompilationOptions::setTargetAbi(const std::string& abi)
{
	m_targetAbi = abi;
}

inline std::string SourceGroupSettingsWithCxxCrossCompilationOptions::getTargetFlag() const
{
	std::string targetFlag;
	if (m_targetOptionsEnabled && !m_targetArch.empty())
	{
		targetFlag = ClangCompiler::targetOption(m_targetArch);
		targetFlag += "-" + (m_targetVendor.empty() ? "unknown" : m_targetVendor);
		targetFlag += "-" + (m_targetSys.empty() ? "unknown" : m_targetSys);
		targetFlag += "-" + (m_targetAbi.empty() ? "unknown" : m_targetAbi);
	}
	return targetFlag;
}

inline bool SourceGroupSettingsWithCxxCrossCompilationOptions::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithCxxCrossCompilationOptions* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithCxxCrossCompilationOptions*>(other);

	return (other && getTargetFlag() == otherPtr->getTargetFlag());
}

inline void SourceGroupSettingsWithCxxCrossCompilationOptions::load(
	const ConfigManager* config, const std::string& key)
{
	setTargetOptionsEnabled(
		config->getValueOrDefault<bool>(key + "/cross_compilation/target_options_enabled", false));
	setTargetArch(
		config->getValueOrDefault<std::string>(key + "/cross_compilation/target/arch", ""));
	setTargetVendor(
		config->getValueOrDefault<std::string>(key + "/cross_compilation/target/vendor", ""));
	setTargetSys(
		config->getValueOrDefault<std::string>(key + "/cross_compilation/target/sys", ""));
	setTargetAbi(
		config->getValueOrDefault<std::string>(key + "/cross_compilation/target/abi", ""));
}

inline void SourceGroupSettingsWithCxxCrossCompilationOptions::save(
	ConfigManager* config, const std::string& key)
{
	config->setValue(key + "/cross_compilation/target_options_enabled", getTargetOptionsEnabled());
	config->setValue(key + "/cross_compilation/target/arch", getTargetArch());
	config->setValue(key + "/cross_compilation/target/vendor", getTargetVendor());
	config->setValue(key + "/cross_compilation/target/sys", getTargetSys());
	config->setValue(key + "/cross_compilation/target/abi", getTargetAbi());
}
