#include "SourceGroupFactoryModuleCustom.h"

#include "SourceGroupCustomCommand.h"
#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsCustomCommand.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

bool SourceGroupFactoryModuleCustom::supports(SourceGroupType type) const
{
	switch (type)
	{
	case SourceGroupType::CUSTOM_COMMAND:
		return true;
	default:
		break;
	}
	return false;
}

std::shared_ptr<SourceGroup> SourceGroupFactoryModuleCustom::createSourceGroup(
	std::shared_ptr<SourceGroupSettings> settings) const
{
	std::shared_ptr<SourceGroup> sourceGroup;
	if (std::shared_ptr<SourceGroupSettingsCustomCommand> customSettings =
			std::dynamic_pointer_cast<SourceGroupSettingsCustomCommand>(settings))
	{
		sourceGroup = std::shared_ptr<SourceGroup>(new SourceGroupCustomCommand(customSettings));
	}
	return sourceGroup;
}
