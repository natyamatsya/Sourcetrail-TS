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
#include "Status.h"
#include "Tree.h"
#include "Property.h"
#include "ScopedSwitcher.h"
