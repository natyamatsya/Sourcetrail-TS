// `srctrl.data:types` partition -- small self-contained data enums/types. Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <array>
#include <string>
#endif

export module srctrl.data:types;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

#define SRCTRL_MODULE_PURVIEW
#include "TooltipOrigin.h"
#include "NameDelimiterType.h"
