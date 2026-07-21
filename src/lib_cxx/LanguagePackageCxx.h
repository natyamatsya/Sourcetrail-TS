#ifndef LANGUAGE_PACKAGE_CXX_H
#define LANGUAGE_PACKAGE_CXX_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "LanguagePackage.h"
#endif

SRCTRL_EXPORT class LanguagePackageCxx: public LanguagePackage
{
public:
	std::vector<std::shared_ptr<IndexerBase>> instantiateSupportedIndexers() const override;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "LanguagePackageCxx.inl"
#endif

#endif	  // LANGUAGE_PACKAGE_CXX_H
