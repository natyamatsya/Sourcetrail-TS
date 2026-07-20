#ifndef LANGUAGE_PACKAGE_H
#define LANGUAGE_PACKAGE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>
#include <vector>

class IndexerBase;
#endif

SRCTRL_EXPORT class LanguagePackage
{
public:
	virtual ~LanguagePackage() = default;
	virtual std::vector<std::shared_ptr<IndexerBase>> instantiateSupportedIndexers() const = 0;
};

#endif	  // LANGUAGE_PACKAGE_H
