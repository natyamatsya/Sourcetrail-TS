#ifndef SOURCE_GROUP_SETTINGS_SWIFT_EMPTY_H
#define SOURCE_GROUP_SETTINGS_SWIFT_EMPTY_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettings.h"
#include "SourceGroupSettingsWithExcludeFilters.h"
#include "SourceGroupSettingsWithSourceExtensions.h"
#include "SourceGroupSettingsWithSourcePaths.h"
#include "SourceGroupSettingsWithSwiftOptions.h"

SRCTRL_EXPORT class SourceGroupSettingsSwiftEmpty
	: public SourceGroupSettings
	, public SourceGroupSettingsWithSourcePaths
	, public SourceGroupSettingsWithExcludeFilters
	, public SourceGroupSettingsWithSourceExtensions
	, public SourceGroupSettingsWithSwiftOptions
{
public:
	std::vector<std::string> getDefaultSourceExtensions() const override;

	SourceGroupSettingsSwiftEmpty(const std::string& id, const ProjectSettings* projectSettings);

	std::shared_ptr<SourceGroupSettings> createCopy() const override;
	void loadSettings(const ConfigManager* config) override;
	void saveSettings(ConfigManager* config) override;
	bool equalsSettings(const SourceGroupSettingsBase* other) override;
};

#include "SourceGroupSettingsSwiftEmpty.inl"

#endif	  // SOURCE_GROUP_SETTINGS_SWIFT_EMPTY_H
