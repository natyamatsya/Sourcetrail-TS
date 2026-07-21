#include "SourceGroupFactoryModuleRust.h"

#include "SourceGroupRust.h"
#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsRustEmpty.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

bool SourceGroupFactoryModuleRust::supports(SourceGroupType type) const
{
	return type == SourceGroupType::RUST_EMPTY;
}

std::shared_ptr<SourceGroup> SourceGroupFactoryModuleRust::createSourceGroup(
	std::shared_ptr<SourceGroupSettings> settings) const
{
	if (auto rustSettings = std::dynamic_pointer_cast<SourceGroupSettingsRustEmpty>(settings))
		return std::make_shared<SourceGroupRust>(rustSettings);
	return nullptr;
}
