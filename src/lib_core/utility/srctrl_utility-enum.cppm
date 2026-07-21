// `srctrl.utility:enum` partition -- the utilityEnum helpers. Module build only.
// `module;` / `export module` are literal (never behind an #ifdef).

module;

#ifndef SRCTRL_IMPORT_STD
#include <limits>
#include <ostream>
#include <string>
#include <type_traits>
#endif

// NB: the partition name is `enums`, not `enum` -- a partition name can't be a keyword.
export module srctrl.utility:enums;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "utilityEnum.h"

// close the purview-wide extern "C++" linkage block
}
