#ifndef SOURCE_GROUP_SETTINGS_CXX_CMAKE_FILE_API_H
#define SOURCE_GROUP_SETTINGS_CXX_CMAKE_FILE_API_H

#include "SourceGroupSettingsWithComponents.h"
#include "SourceGroupSettingsWithCxxCMakeBuildDirectory.h"
#include "SourceGroupSettingsWithCxxPathsAndFlags.h"
#include "SourceGroupSettingsWithExcludeFilters.h"
#include "SourceGroupSettingsWithIndexedHeaderPaths.h"

class SourceGroupSettingsCxxCMakeFileAPI
	: public SourceGroupSettingsWithComponents<
		  SourceGroupSettingsWithCxxCMakeBuildDirectory,
		  SourceGroupSettingsWithCxxPathsAndFlags,
		  SourceGroupSettingsWithExcludeFilters,
		  SourceGroupSettingsWithIndexedHeaderPaths>
{
public:
	SourceGroupSettingsCxxCMakeFileAPI(
		const std::string& id, const ProjectSettings* projectSettings)
		: SourceGroupSettingsWithComponents(
			  SourceGroupType::CXX_CMAKE_FILE_API, id, projectSettings)
	{
	}

	std::shared_ptr<SourceGroupSettings> createCopy() const override
	{
		return std::make_shared<SourceGroupSettingsCxxCMakeFileAPI>(*this);
	}
};

#endif	  // SOURCE_GROUP_SETTINGS_CXX_CMAKE_FILE_API_H
