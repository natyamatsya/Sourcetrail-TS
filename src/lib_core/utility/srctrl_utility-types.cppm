// `srctrl.utility:types` partition -- small self-contained value types/utilities. Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <string>
#include <vector>
#endif

export module srctrl.utility:types;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "Status.h"
#include "Tree.h"
#include "Property.h"
#include "ScopedSwitcher.h"

// close the purview-wide extern "C++" linkage block
}
