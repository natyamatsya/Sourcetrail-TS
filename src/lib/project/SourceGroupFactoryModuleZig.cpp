#include "SourceGroupFactoryModuleZig.h"

#include "SourceGroupSettingsZigEmpty.h"
#include "SourceGroupZig.h"

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
