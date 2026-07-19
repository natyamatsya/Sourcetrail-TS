#include "LanguagePackageZig.h"

std::vector<std::shared_ptr<IndexerBase>> LanguagePackageZig::instantiateSupportedIndexers() const
{
	// The Zig indexer runs as a separate process (sourcetrail_zig_indexer).
	// No in-process indexer is needed here.
	return {};
}
