// `srctrl.data:types` partition -- small self-contained data enums/types. Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <array>
#include <cstddef>
#include <string>
#endif

export module srctrl.data:types;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

// Cross-module dependency: LocationType uses utilityEnum's intToEnum/lookupEnum. Importing before the
// purview means the header build's `#include "utilityEnum.h"` is skipped and these come from the module
// instead. This is the migration's first inter-module `import`.
import srctrl.utility;

#define SRCTRL_MODULE_PURVIEW
#include "TooltipOrigin.h"
#include "NameDelimiterType.h"
#include "LocationType.h"
