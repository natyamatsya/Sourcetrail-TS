// Inline implementations for IndexerCommandProvider.h. Included at the end of that header (classic) or via
// the srctrl.indexer wrapper (purview); not a standalone TU.

#pragma once

inline bool IndexerCommandProvider::empty() const
{
	return size() == 0;
}
