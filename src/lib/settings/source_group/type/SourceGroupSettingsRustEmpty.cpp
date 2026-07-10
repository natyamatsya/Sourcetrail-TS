#include "SourceGroupSettingsRustEmpty.h"

#include "ConfigManager.h"

std::vector<std::string> SourceGroupSettingsRustEmpty::getDefaultSourceExtensions() const
{
	return {".rs"};
}

SourceGroupSettingsRustEmpty::SourceGroupSettingsRustEmpty(
	const std::string& id, const ProjectSettings* projectSettings)
	: SourceGroupSettings(SourceGroupType::RUST_EMPTY, id, projectSettings)
{
}

std::shared_ptr<SourceGroupSettings> SourceGroupSettingsRustEmpty::createCopy() const
{
	return std::make_shared<SourceGroupSettingsRustEmpty>(*this);
}

void SourceGroupSettingsRustEmpty::loadSettings(const ConfigManager* config)
{
	const std::string key = s_keyPrefix + getId();
	SourceGroupSettings::load(config, key);
	SourceGroupSettingsWithSourcePaths::load(config, key);
	SourceGroupSettingsWithExcludeFilters::load(config, key);
	SourceGroupSettingsWithSourceExtensions::load(config, key);
	SourceGroupSettingsWithCargoOptions::load(config, key);
}

void SourceGroupSettingsRustEmpty::saveSettings(ConfigManager* config)
{
	const std::string key = s_keyPrefix + getId();
	SourceGroupSettings::save(config, key);
	SourceGroupSettingsWithSourcePaths::save(config, key);
	SourceGroupSettingsWithExcludeFilters::save(config, key);
	SourceGroupSettingsWithSourceExtensions::save(config, key);
	SourceGroupSettingsWithCargoOptions::save(config, key);
}

bool SourceGroupSettingsRustEmpty::equalsSettings(const SourceGroupSettingsBase* other)
{
	if (!SourceGroupSettings::equals(other))
		return false;
	if (!SourceGroupSettingsWithSourcePaths::equals(other))
		return false;
	if (!SourceGroupSettingsWithExcludeFilters::equals(other))
		return false;
	if (!SourceGroupSettingsWithSourceExtensions::equals(other))
		return false;
	return true;
}
