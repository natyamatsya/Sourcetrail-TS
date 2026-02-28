#include "SourceGroupFactoryModuleSwift.h"

#if BUILD_SWIFT_LANGUAGE_PACKAGE

#include "SourceGroupSettingsSwiftEmpty.h"
#include "SourceGroupSwift.h"

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

#endif	  // BUILD_SWIFT_LANGUAGE_PACKAGE
