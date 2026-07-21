#ifndef SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_CXX_H
#define SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_CXX_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsWithSourceExtensions.h"

SRCTRL_EXPORT class SourceGroupSettingsWithSourceExtensionsCxx: public SourceGroupSettingsWithSourceExtensions
{
private:
	std::vector<std::string> getDefaultSourceExtensions() const override
	{
		return {".c", ".cpp", ".cxx", ".cc"};
	}
};

#endif	  // SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_CXX_H
