#ifndef SOURCE_GROUP_SETTINGS_CXX_CDB_H
#define SOURCE_GROUP_SETTINGS_CXX_CDB_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "SourceGroupSettingsWithComponents.h"
#include "SourceGroupSettingsWithCxxCdbPath.h"
#include "SourceGroupSettingsWithCxxPathsAndFlags.h"
#include "SourceGroupSettingsWithCxxPchOptions.h"
#include "SourceGroupSettingsWithExcludeFilters.h"
#include "SourceGroupSettingsWithIndexedHeaderPaths.h"

SRCTRL_EXPORT class SourceGroupSettingsCxxCdb
	: public SourceGroupSettingsWithComponents<
		  SourceGroupSettingsWithCxxCdbPath,
		  SourceGroupSettingsWithCxxPathsAndFlags,
		  SourceGroupSettingsWithCxxPchOptions,
		  SourceGroupSettingsWithExcludeFilters,
		  SourceGroupSettingsWithIndexedHeaderPaths>
{
public:
	SourceGroupSettingsCxxCdb(const std::string& id, const ProjectSettings* projectSettings)
		: SourceGroupSettingsWithComponents(SourceGroupType::CXX_CDB, id, projectSettings)
	{
	}

	std::shared_ptr<SourceGroupSettings> createCopy() const override
	{
		return std::make_shared<SourceGroupSettingsCxxCdb>(*this);
	}

protected:
	// For a group whose compilation database Sourcetrail writes (from a Makefile).
	SourceGroupSettingsCxxCdb(SourceGroupType type, const std::string& id, const ProjectSettings* projectSettings)
		: SourceGroupSettingsWithComponents(type, id, projectSettings)
	{
	}
};

#endif	  // SOURCE_GROUP_SETTINGS_CXX_CDB_H
