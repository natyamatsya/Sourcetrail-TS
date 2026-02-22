#include "LanguagePackageRust.h"

std::vector<std::shared_ptr<IndexerBase>> LanguagePackageRust::instantiateSupportedIndexers() const
{
	// The Rust indexer runs as a separate process (sourcetrail_rust_indexer).
	// No in-process indexer is needed here.
	return {};
}
