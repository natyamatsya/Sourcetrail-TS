#ifndef SOURCE_GROUP_FACTORY_MODULE_SWIFT_H
#define SOURCE_GROUP_FACTORY_MODULE_SWIFT_H

#include "language_packages.h"

#if BUILD_SWIFT_LANGUAGE_PACKAGE

#include "SourceGroupFactoryModule.h"

class SourceGroupFactoryModuleSwift: public SourceGroupFactoryModule
{
public:
	bool supports(SourceGroupType type) const override;
	std::shared_ptr<SourceGroup> createSourceGroup(
		std::shared_ptr<SourceGroupSettings> settings) const override;
};

#endif	  // BUILD_SWIFT_LANGUAGE_PACKAGE

#endif	  // SOURCE_GROUP_FACTORY_MODULE_SWIFT_H
