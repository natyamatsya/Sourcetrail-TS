#ifndef LANGUAGE_PACKAGE_RUST_H
#define LANGUAGE_PACKAGE_RUST_H

#include "LanguagePackage.h"

class LanguagePackageRust: public LanguagePackage
{
public:
	std::vector<std::shared_ptr<IndexerBase>> instantiateSupportedIndexers() const override;
};

#endif	  // LANGUAGE_PACKAGE_RUST_H
