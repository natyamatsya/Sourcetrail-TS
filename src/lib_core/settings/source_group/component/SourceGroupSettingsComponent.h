#ifndef SOURCE_GROUP_SETTINGS_COMPONENT_H
#define SOURCE_GROUP_SETTINGS_COMPONENT_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsBase.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#endif

#ifndef SRCTRL_MODULE_PURVIEW
class ConfigManager;
#endif

SRCTRL_EXPORT class SourceGroupSettingsComponent: virtual public SourceGroupSettingsBase
{
public:
	~SourceGroupSettingsComponent() override = default;

protected:
	virtual void load(const ConfigManager* config, const std::string& key) = 0;
	virtual void save(ConfigManager* config, const std::string& key) = 0;

	virtual bool equals(const SourceGroupSettingsBase* other) const = 0;
};

#endif	  // SOURCE_GROUP_SETTINGS_COMPONENT_H
