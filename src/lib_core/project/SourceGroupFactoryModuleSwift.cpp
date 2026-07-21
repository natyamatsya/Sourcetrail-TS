#include "SourceGroupFactoryModuleSwift.h"

#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsSwiftEmpty.h"
#endif
#include "SourceGroupSwift.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

bool SourceGroupFactoryModuleSwift::supports(SourceGroupType type) const
{
	return type == SourceGroupType::SWIFT_EMPTY;
}

std::shared_ptr<SourceGroup> SourceGroupFactoryModuleSwift::createSourceGroup(
	std::shared_ptr<SourceGroupSettings> settings) const
{
	if (auto swiftSettings = std::dynamic_pointer_cast<SourceGroupSettingsSwiftEmpty>(settings))
		return std::make_shared<SourceGroupSwift>(swiftSettings);
	return nullptr;
}
