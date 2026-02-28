#ifndef LANGUAGE_PACKAGE_SWIFT_H
#define LANGUAGE_PACKAGE_SWIFT_H

#include "LanguagePackage.h"

class LanguagePackageSwift: public LanguagePackage
{
public:
	std::vector<std::shared_ptr<IndexerBase>> instantiateSupportedIndexers() const override;
};

#endif	  // LANGUAGE_PACKAGE_SWIFT_H
