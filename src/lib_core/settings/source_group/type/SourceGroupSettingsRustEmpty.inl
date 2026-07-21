// Inline implementations for SourceGroupSettingsRustEmpty.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "ConfigManager.h"
#endif

inline std::vector<std::string> SourceGroupSettingsRustEmpty::getDefaultSourceExtensions() const
{
	return {".rs"};
}

inline SourceGroupSettingsRustEmpty::SourceGroupSettingsRustEmpty(
	const std::string& id, const ProjectSettings* projectSettings)
	: SourceGroupSettings(SourceGroupType::RUST_EMPTY, id, projectSettings)
{
}

inline std::shared_ptr<SourceGroupSettings> SourceGroupSettingsRustEmpty::createCopy() const
{
	return std::make_shared<SourceGroupSettingsRustEmpty>(*this);
}

inline void SourceGroupSettingsRustEmpty::loadSettings(const ConfigManager* config)
{
	const std::string key = s_keyPrefix + getId();
	SourceGroupSettings::load(config, key);
	SourceGroupSettingsWithSourcePaths::load(config, key);
	SourceGroupSettingsWithExcludeFilters::load(config, key);
	SourceGroupSettingsWithSourceExtensions::load(config, key);
	SourceGroupSettingsWithCargoOptions::load(config, key);
}

inline void SourceGroupSettingsRustEmpty::saveSettings(ConfigManager* config)
{
	const std::string key = s_keyPrefix + getId();
	SourceGroupSettings::save(config, key);
	SourceGroupSettingsWithSourcePaths::save(config, key);
	SourceGroupSettingsWithExcludeFilters::save(config, key);
	SourceGroupSettingsWithSourceExtensions::save(config, key);
	SourceGroupSettingsWithCargoOptions::save(config, key);
}

inline bool SourceGroupSettingsRustEmpty::equalsSettings(const SourceGroupSettingsBase* other)
{
	if (!SourceGroupSettings::equals(other))
		return false;
	if (!SourceGroupSettingsWithSourcePaths::equals(other))
		return false;
	if (!SourceGroupSettingsWithExcludeFilters::equals(other))
		return false;
	if (!SourceGroupSettingsWithSourceExtensions::equals(other))
		return false;
	if (!SourceGroupSettingsWithCargoOptions::equals(other))
		return false;
	return true;
}
