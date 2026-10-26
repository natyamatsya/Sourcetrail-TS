#ifndef SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_CPP_H
#define SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_CPP_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsWithSourceExtensions.h"

SRCTRL_EXPORT class SourceGroupSettingsWithSourceExtensionsCpp: public SourceGroupSettingsWithSourceExtensions
{
private:
	// The C++20 module interface extensions CMake knows are sources too: leaving them out drops a
	// modules project's interface units from the group, and every `import` of them fails.
	std::vector<std::string> getDefaultSourceExtensions() const override
	{
		return {".cpp", ".cxx", ".cc", ".cppm", ".ixx", ".cxxm", ".ccm", ".c++m"};
	}
};

#endif	  // SOURCE_GROUP_SETTINGS_WITH_SOURCE_EXTENSIONS_CPP_H
