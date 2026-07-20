#include "SourceGroupSettingsZigEmpty.h"

#include "ConfigManager.h"

std::vector<std::string> SourceGroupSettingsZigEmpty::getDefaultSourceExtensions() const
{
	return {".zig"};
}

SourceGroupSettingsZigEmpty::SourceGroupSettingsZigEmpty(
	const std::string& id, const ProjectSettings* projectSettings)
	: SourceGroupSettings(SourceGroupType::ZIG_EMPTY, id, projectSettings)
{
}

std::shared_ptr<SourceGroupSettings> SourceGroupSettingsZigEmpty::createCopy() const
{
	return std::make_shared<SourceGroupSettingsZigEmpty>(*this);
}

void SourceGroupSettingsZigEmpty::loadSettings(const ConfigManager* config)
{
	const std::string key = s_keyPrefix + getId();
	SourceGroupSettings::load(config, key);
	SourceGroupSettingsWithSourcePaths::load(config, key);
	SourceGroupSettingsWithExcludeFilters::load(config, key);
	SourceGroupSettingsWithSourceExtensions::load(config, key);
}

void SourceGroupSettingsZigEmpty::saveSettings(ConfigManager* config)
{
	const std::string key = s_keyPrefix + getId();
	SourceGroupSettings::save(config, key);
	SourceGroupSettingsWithSourcePaths::save(config, key);
	SourceGroupSettingsWithExcludeFilters::save(config, key);
	SourceGroupSettingsWithSourceExtensions::save(config, key);
}

bool SourceGroupSettingsZigEmpty::equalsSettings(const SourceGroupSettingsBase* other)
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
