#ifndef SOURCE_GROUP_SETTINGS_RUST_EMPTY_H
#define SOURCE_GROUP_SETTINGS_RUST_EMPTY_H

#include "SourceGroupSettings.h"
#include "SourceGroupSettingsWithCargoOptions.h"
#include "SourceGroupSettingsWithExcludeFilters.h"
#include "SourceGroupSettingsWithSourceExtensions.h"
#include "SourceGroupSettingsWithSourcePaths.h"

class SourceGroupSettingsRustEmpty
	: public SourceGroupSettings
	, public SourceGroupSettingsWithSourcePaths
	, public SourceGroupSettingsWithExcludeFilters
	, public SourceGroupSettingsWithSourceExtensions
	, public SourceGroupSettingsWithCargoOptions
{
public:
	std::vector<std::string> getDefaultSourceExtensions() const override;

	SourceGroupSettingsRustEmpty(const std::string& id, const ProjectSettings* projectSettings);

	std::shared_ptr<SourceGroupSettings> createCopy() const override;
	void loadSettings(const ConfigManager* config) override;
	void saveSettings(ConfigManager* config) override;
	bool equalsSettings(const SourceGroupSettingsBase* other) override;
};

#endif	  // SOURCE_GROUP_SETTINGS_RUST_EMPTY_H
