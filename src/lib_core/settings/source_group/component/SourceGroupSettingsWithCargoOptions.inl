// Inline implementations for SourceGroupSettingsWithCargoOptions.h (included at its end). All definitions inline: the family
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

inline const FilePath& SourceGroupSettingsWithCargoOptions::getCargoWorkspaceDirectory() const
{
	return m_cargoWorkspaceDirectory;
}

inline FilePath SourceGroupSettingsWithCargoOptions::getCargoWorkspaceDirectoryExpandedAndAbsolute() const
{
	if (m_cargoWorkspaceDirectory.empty())
		return FilePath();
	return utility::getExpandedAndAbsolutePath(
		m_cargoWorkspaceDirectory, getProjectSettings()->getProjectDirectoryPath());
}

inline void SourceGroupSettingsWithCargoOptions::setCargoWorkspaceDirectory(const FilePath& path)
{
	m_cargoWorkspaceDirectory = path;
}

inline const std::vector<std::string>& SourceGroupSettingsWithCargoOptions::getCargoFeatures() const
{
	return m_cargoFeatures;
}

inline void SourceGroupSettingsWithCargoOptions::setCargoFeatures(const std::vector<std::string>& features)
{
	m_cargoFeatures = features;
}

inline bool SourceGroupSettingsWithCargoOptions::getCargoAllFeatures() const
{
	return m_cargoAllFeatures;
}

inline void SourceGroupSettingsWithCargoOptions::setCargoAllFeatures(bool allFeatures)
{
	m_cargoAllFeatures = allFeatures;
}

inline bool SourceGroupSettingsWithCargoOptions::getCargoNoDefaultFeatures() const
{
	return m_cargoNoDefaultFeatures;
}

inline void SourceGroupSettingsWithCargoOptions::setCargoNoDefaultFeatures(bool noDefaultFeatures)
{
	m_cargoNoDefaultFeatures = noDefaultFeatures;
}

inline const std::string& SourceGroupSettingsWithCargoOptions::getCargoTargetTriple() const
{
	return m_cargoTargetTriple;
}

inline void SourceGroupSettingsWithCargoOptions::setCargoTargetTriple(const std::string& targetTriple)
{
	m_cargoTargetTriple = targetTriple;
}

inline const std::string& SourceGroupSettingsWithCargoOptions::getRustSpecializationScope() const
{
	return m_rustSpecializationScope;
}

inline void SourceGroupSettingsWithCargoOptions::setRustSpecializationScope(const std::string& scope)
{
	m_rustSpecializationScope = scope;
}

inline bool SourceGroupSettingsWithCargoOptions::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithCargoOptions* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithCargoOptions*>(other);

	return (
		otherPtr && m_cargoWorkspaceDirectory == otherPtr->m_cargoWorkspaceDirectory &&
		utility::isPermutation(m_cargoFeatures, otherPtr->m_cargoFeatures) &&
		m_cargoAllFeatures == otherPtr->m_cargoAllFeatures &&
		m_cargoNoDefaultFeatures == otherPtr->m_cargoNoDefaultFeatures &&
		m_cargoTargetTriple == otherPtr->m_cargoTargetTriple &&
		m_rustSpecializationScope == otherPtr->m_rustSpecializationScope);
}

inline void SourceGroupSettingsWithCargoOptions::load(const ConfigManager* config, const std::string& key)
{
	setCargoWorkspaceDirectory(FilePath(
		config->getValueOrDefault(key + "/cargo_workspace_directory", std::string{})));
	setCargoFeatures(config->getValuesOrDefaults(
		key + "/cargo_features/cargo_feature", std::vector<std::string>()));
	setCargoAllFeatures(config->getValueOrDefault(key + "/cargo_all_features", false));
	setCargoNoDefaultFeatures(config->getValueOrDefault(key + "/cargo_no_default_features", false));
	setCargoTargetTriple(config->getValueOrDefault<std::string>(key + "/cargo_target_triple", ""));
	setRustSpecializationScope(
		config->getValueOrDefault<std::string>(key + "/rust_specialization_scope", "local"));
}

inline void SourceGroupSettingsWithCargoOptions::save(ConfigManager* config, const std::string& key)
{
	config->setValue(key + "/cargo_workspace_directory", getCargoWorkspaceDirectory().str());
	config->setValues(key + "/cargo_features/cargo_feature", getCargoFeatures());
	config->setValue(key + "/cargo_all_features", getCargoAllFeatures());
	config->setValue(key + "/cargo_no_default_features", getCargoNoDefaultFeatures());
	config->setValue(key + "/cargo_target_triple", getCargoTargetTriple());
	config->setValue(key + "/rust_specialization_scope", getRustSpecializationScope());
}
