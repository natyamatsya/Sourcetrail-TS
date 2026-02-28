#include "LanguagePackageSwift.h"

std::vector<std::shared_ptr<IndexerBase>> LanguagePackageSwift::instantiateSupportedIndexers() const
{
	// The Swift indexer runs as a separate process (sourcetrail_swift_indexer).
	// No in-process indexer is needed here.
	return {};
}
