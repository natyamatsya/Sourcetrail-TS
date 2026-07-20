#ifndef LANGUAGE_PACKAGE_ZIG_H
#define LANGUAGE_PACKAGE_ZIG_H

#include "LanguagePackage.h"

class LanguagePackageZig: public LanguagePackage
{
public:
	std::vector<std::shared_ptr<IndexerBase>> instantiateSupportedIndexers() const override;
};

#endif	  // LANGUAGE_PACKAGE_ZIG_H
