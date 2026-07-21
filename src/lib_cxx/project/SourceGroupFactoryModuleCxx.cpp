#include "SourceGroupFactoryModuleCxx.h"

#include "SourceGroupCxxCdb.h"
#include "SourceGroupCxxCMakeFileAPI.h"
#include "SourceGroupCxxEmpty.h"
#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsCEmpty.h"
#include "SourceGroupSettingsCppEmpty.h"
#include "SourceGroupSettingsCxxCdb.h"
#include "SourceGroupSettingsCxxCMakeFileAPI.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

bool SourceGroupFactoryModuleCxx::supports(SourceGroupType type) const
{
	switch (type)
	{
	case SourceGroupType::C_EMPTY:
	case SourceGroupType::CXX_EMPTY:
	case SourceGroupType::CXX_CDB:
	case SourceGroupType::CXX_CMAKE_FILE_API:
		return true;
	default:
		break;
	}
	return false;
}

std::shared_ptr<SourceGroup> SourceGroupFactoryModuleCxx::createSourceGroup(
	std::shared_ptr<SourceGroupSettings> settings) const
{
	std::shared_ptr<SourceGroup> sourceGroup;
	if (std::shared_ptr<SourceGroupSettingsCxxCdb> cxxSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsCxxCdb>(settings))
	{
		sourceGroup = std::shared_ptr<SourceGroup>(new SourceGroupCxxCdb(cxxSettings));
	}
	else if (
		std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> cxxSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsCxxCMakeFileAPI>(settings))
	{
		sourceGroup = std::shared_ptr<SourceGroup>(new SourceGroupCxxCMakeFileAPI(cxxSettings));
	}
	else if (
		std::shared_ptr<SourceGroupSettingsCEmpty> cxxSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsCEmpty>(settings))
	{
		sourceGroup = std::shared_ptr<SourceGroup>(new SourceGroupCxxEmpty(cxxSettings));
	}
	else if (
		std::shared_ptr<SourceGroupSettingsCppEmpty> cxxSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsCppEmpty>(settings))
	{
		sourceGroup = std::shared_ptr<SourceGroup>(new SourceGroupCxxEmpty(cxxSettings));
	}
	return sourceGroup;
}
