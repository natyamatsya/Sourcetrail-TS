#ifndef LANGUAGE_PACKAGE_MANAGER_H
#define LANGUAGE_PACKAGE_MANAGER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>
#include <vector>
#endif

class IndexerComposite;
class LanguagePackage;

SRCTRL_EXPORT class LanguagePackageManager
{
public:
	static std::shared_ptr<LanguagePackageManager> getInstance();
	static void destroyInstance();

	void addPackage(std::shared_ptr<LanguagePackage> package);
	std::shared_ptr<IndexerComposite> instantiateSupportedIndexers();

private:
	static std::shared_ptr<LanguagePackageManager> s_instance;
	LanguagePackageManager() = default;

	std::vector<std::shared_ptr<LanguagePackage>> m_packages;
};

#ifndef SRCTRL_MODULE_PURVIEW
#include "LanguagePackageManager.inl"
#endif

#endif	  // LANGUAGE_PACKAGE_MANAGER_H
