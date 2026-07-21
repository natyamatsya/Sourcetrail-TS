// Inline implementations for LanguagePackageCxx.h. Included at the end of that header (classic) or
// via the srctrl.cxx:package wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerCxx.h"
#endif

inline std::vector<std::shared_ptr<IndexerBase>> LanguagePackageCxx::instantiateSupportedIndexers() const
{
	return {
		std::make_shared<IndexerCxx>(),
	};
}
