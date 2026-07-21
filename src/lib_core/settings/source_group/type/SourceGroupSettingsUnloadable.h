#ifndef SOURCE_GROUP_SETTINGS_UNLOADABLE_H
#define SOURCE_GROUP_SETTINGS_UNLOADABLE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettings.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <map>
#include <string>
#endif

SRCTRL_EXPORT class SourceGroupSettingsUnloadable: public SourceGroupSettings
{
SRCTRL_EXPORT public:
	SourceGroupSettingsUnloadable(const std::string& id, const ProjectSettings* projectSettings);
	std::string getTypeString();
	std::shared_ptr<SourceGroupSettings> createCopy() const override;
	void loadSettings(const ConfigManager* config) override;
	void saveSettings(ConfigManager* config) override;
	bool equalsSettings(const SourceGroupSettingsBase* other) override;

private:
	std::string m_typeString;
	std::map<std::string, std::vector<std::string>> m_content;
};

#include "SourceGroupSettingsUnloadable.inl"

#endif	  // SOURCE_GROUP_SETTINGS_UNLOADABLE_H
