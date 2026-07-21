#ifndef SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_CPP_H
#define SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_CPP_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsWithSourceExtensions.h"

SRCTRL_EXPORT class SourceGroupSettingsWithSourceExtensionsCpp: public SourceGroupSettingsWithSourceExtensions
{
private:
	std::vector<std::string> getDefaultSourceExtensions() const override
	{
		return {".cpp", ".cxx", ".cc"};
	}
};

#endif	  // SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_CPP_H
