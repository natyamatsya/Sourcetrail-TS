// `srctrl.data:tooltip` partition -- TooltipInfo / TooltipSnippet. Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <memory>
#include <string>
#include <vector>
#endif

#include "types.h"

export module srctrl.data:tooltip;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // Vec2i (srctrl.utility:math)
import :location;        // SourceLocationFile (shared_ptr member of TooltipSnippet)

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "TooltipInfo.h"

// close the purview-wide extern "C++" linkage block
}
