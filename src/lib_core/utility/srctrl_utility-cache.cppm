// `srctrl.utility:cache` partition -- the cache helpers. Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <functional>
#include <map>
#include <unordered_map>
#endif

export module srctrl.utility:cache;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "SingleValueCache.h"
#include "OrderedCache.h"
#include "UnorderedCache.h"

// close the purview-wide extern "C++" linkage block
}
