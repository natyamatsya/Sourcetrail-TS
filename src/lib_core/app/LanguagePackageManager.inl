// Inline implementations for LanguagePackageManager.h. Included at the end of that header
// (classic) or via the srctrl.indexer wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerComposite.h"
#include "LanguagePackage.h"
#endif

inline std::shared_ptr<LanguagePackageManager> LanguagePackageManager::getInstance()
{
	if (!s_instance)
	{
		s_instance = std::shared_ptr<LanguagePackageManager>(new LanguagePackageManager());
	}
	return s_instance;
}

inline void LanguagePackageManager::destroyInstance()
{
	s_instance.reset();
}

inline void LanguagePackageManager::addPackage(std::shared_ptr<LanguagePackage> package)
{
	m_packages.push_back(package);
}

inline std::shared_ptr<IndexerComposite> LanguagePackageManager::instantiateSupportedIndexers()
{
	std::shared_ptr<IndexerComposite> composite = std::make_shared<IndexerComposite>();
	for (const std::shared_ptr<LanguagePackage> &package: m_packages)
	{
		for (const std::shared_ptr<IndexerBase> &indexer: package->instantiateSupportedIndexers())
		{
			composite->addIndexer(indexer);
		}
	}
	return composite;
}

// One program-wide singleton slot (inline variable), whichever TU or module unit reaches it first.
inline std::shared_ptr<LanguagePackageManager> LanguagePackageManager::s_instance;
