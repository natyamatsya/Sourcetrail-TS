// Inline implementations for SourceGroupSettingsWithSwiftOptions.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Family-internal includes stay unguarded: same module either way; include guards
// + inl-after-class ordering make the cross-references resolve in both builds.
#include "ProjectSettings.h"

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "ConfigManager.h"
#include "utility.h"
#include "utilityFile.h"
#endif

inline const std::vector<std::string>& SourceGroupSettingsWithSwiftOptions::getSwiftBuildArgs() const
{
	return m_swiftBuildArgs;
}

inline void SourceGroupSettingsWithSwiftOptions::setSwiftBuildArgs(const std::vector<std::string>& args)
{
	m_swiftBuildArgs = args;
}

inline const FilePath& SourceGroupSettingsWithSwiftOptions::getSwiftToolchainPath() const
{
	return m_swiftToolchainPath;
}

inline FilePath SourceGroupSettingsWithSwiftOptions::getSwiftToolchainPathExpandedAndAbsolute() const
{
	if (m_swiftToolchainPath.empty())
		return FilePath();
	return utility::getExpandedAndAbsolutePath(
		m_swiftToolchainPath, getProjectSettings()->getProjectDirectoryPath());
}

inline void SourceGroupSettingsWithSwiftOptions::setSwiftToolchainPath(const FilePath& path)
{
	m_swiftToolchainPath = path;
}

inline const FilePath& SourceGroupSettingsWithSwiftOptions::getSwiftIndexStorePath() const
{
	return m_swiftIndexStorePath;
}

inline FilePath SourceGroupSettingsWithSwiftOptions::getSwiftIndexStorePathExpandedAndAbsolute() const
{
	if (m_swiftIndexStorePath.empty())
		return FilePath();
	return utility::getExpandedAndAbsolutePath(
		m_swiftIndexStorePath, getProjectSettings()->getProjectDirectoryPath());
}

inline void SourceGroupSettingsWithSwiftOptions::setSwiftIndexStorePath(const FilePath& path)
{
	m_swiftIndexStorePath = path;
}

inline const std::string& SourceGroupSettingsWithSwiftOptions::getSwiftSpecializationScope() const
{
	return m_swiftSpecializationScope;
}

inline void SourceGroupSettingsWithSwiftOptions::setSwiftSpecializationScope(const std::string& scope)
{
	m_swiftSpecializationScope = scope;
}

inline bool SourceGroupSettingsWithSwiftOptions::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithSwiftOptions* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithSwiftOptions*>(other);

	return (
		otherPtr && m_swiftBuildArgs == otherPtr->m_swiftBuildArgs &&
		m_swiftToolchainPath == otherPtr->m_swiftToolchainPath &&
		m_swiftIndexStorePath == otherPtr->m_swiftIndexStorePath &&
		m_swiftSpecializationScope == otherPtr->m_swiftSpecializationScope);
}

inline void SourceGroupSettingsWithSwiftOptions::load(const ConfigManager* config, const std::string& key)
{
	setSwiftBuildArgs(config->getValuesOrDefaults(
		key + "/swift_build_args/swift_build_arg", std::vector<std::string>()));
	setSwiftToolchainPath(
		FilePath(config->getValueOrDefault(key + "/swift_toolchain_path", std::string{})));
	setSwiftIndexStorePath(
		FilePath(config->getValueOrDefault(key + "/swift_index_store_path", std::string{})));
	setSwiftSpecializationScope(
		config->getValueOrDefault(key + "/swift_specialization_scope", std::string{"local"}));
}

inline void SourceGroupSettingsWithSwiftOptions::save(ConfigManager* config, const std::string& key)
{
	config->setValues(key + "/swift_build_args/swift_build_arg", getSwiftBuildArgs());
	config->setValue(key + "/swift_toolchain_path", getSwiftToolchainPath().str());
	config->setValue(key + "/swift_index_store_path", getSwiftIndexStorePath().str());
	config->setValue(key + "/swift_specialization_scope", getSwiftSpecializationScope());
}
