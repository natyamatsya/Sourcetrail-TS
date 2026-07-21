#ifndef SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_EMPTY_H
#define SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_EMPTY_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsWithSourceExtensions.h"

SRCTRL_EXPORT class SourceGroupSettingsWithSourceExtensionsEmpty: public SourceGroupSettingsWithSourceExtensions
{
private:
	std::vector<std::string> getDefaultSourceExtensions() const override
	{
		return {};
	}
};

#endif	  // SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_EMPTY_H
