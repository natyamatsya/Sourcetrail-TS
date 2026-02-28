#include "SourceGroupSettingsSwiftEmpty.h"

#if BUILD_SWIFT_LANGUAGE_PACKAGE

#include "ConfigManager.h"

std::vector<std::string> SourceGroupSettingsSwiftEmpty::getDefaultSourceExtensions() const
{
	return {".swift"};
}

SourceGroupSettingsSwiftEmpty::SourceGroupSettingsSwiftEmpty(
	const std::string& id, const ProjectSettings* projectSettings)
	: SourceGroupSettings{SourceGroupType::SWIFT_EMPTY, id, projectSettings}
{
}

std::shared_ptr<SourceGroupSettings> SourceGroupSettingsSwiftEmpty::createCopy() const
{
	return std::make_shared<SourceGroupSettingsSwiftEmpty>(*this);
}

void SourceGroupSettingsSwiftEmpty::loadSettings(const ConfigManager* config)
{
	const std::string key = s_keyPrefix + getId();
	SourceGroupSettings::load(config, key);
	SourceGroupSettingsWithSourcePaths::load(config, key);
	SourceGroupSettingsWithExcludeFilters::load(config, key);
	SourceGroupSettingsWithSourceExtensions::load(config, key);
}

void SourceGroupSettingsSwiftEmpty::saveSettings(ConfigManager* config)
{
	const std::string key = s_keyPrefix + getId();
	SourceGroupSettings::save(config, key);
	SourceGroupSettingsWithSourcePaths::save(config, key);
	SourceGroupSettingsWithExcludeFilters::save(config, key);
	SourceGroupSettingsWithSourceExtensions::save(config, key);
}

bool SourceGroupSettingsSwiftEmpty::equalsSettings(const SourceGroupSettingsBase* other)
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

#endif	  // BUILD_SWIFT_LANGUAGE_PACKAGE
