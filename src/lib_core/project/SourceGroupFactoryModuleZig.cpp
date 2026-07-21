#include "SourceGroupFactoryModuleZig.h"

#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsZigEmpty.h"
#endif
#include "SourceGroupZig.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

bool SourceGroupFactoryModuleZig::supports(SourceGroupType type) const
{
	return type == SourceGroupType::ZIG_EMPTY;
}

std::shared_ptr<SourceGroup> SourceGroupFactoryModuleZig::createSourceGroup(
	std::shared_ptr<SourceGroupSettings> settings) const
{
	if (auto zigSettings = std::dynamic_pointer_cast<SourceGroupSettingsZigEmpty>(settings))
		return std::make_shared<SourceGroupZig>(zigSettings);
	return nullptr;
}
