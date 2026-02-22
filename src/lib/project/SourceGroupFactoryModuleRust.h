#ifndef SOURCE_GROUP_FACTORY_MODULE_RUST_H
#define SOURCE_GROUP_FACTORY_MODULE_RUST_H

#include "language_packages.h"

#if BUILD_RUST_LANGUAGE_PACKAGE

#include "SourceGroupFactoryModule.h"

class SourceGroupFactoryModuleRust: public SourceGroupFactoryModule
{
public:
	bool supports(SourceGroupType type) const override;
	std::shared_ptr<SourceGroup> createSourceGroup(
		std::shared_ptr<SourceGroupSettings> settings) const override;
};

#endif	  // BUILD_RUST_LANGUAGE_PACKAGE

#endif	  // SOURCE_GROUP_FACTORY_MODULE_RUST_H
