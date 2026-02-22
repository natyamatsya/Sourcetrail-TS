#include "SourceGroupFactoryModuleRust.h"

#if BUILD_RUST_LANGUAGE_PACKAGE

#include "SourceGroupRust.h"
#include "SourceGroupSettingsRustEmpty.h"

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

#endif	  // BUILD_RUST_LANGUAGE_PACKAGE
