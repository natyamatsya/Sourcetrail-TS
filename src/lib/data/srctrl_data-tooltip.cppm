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
#include "TooltipInfo.h"
