// `srctrl.data:name` partition -- the name types (NameElement; NameHierarchy follows). Module build
// only. Uses utilityString from srctrl.utility.

module;

#ifndef SRCTRL_IMPORT_STD
#include <string>
#endif

export module srctrl.data:name;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;

#define SRCTRL_MODULE_PURVIEW
#include "NameElement.h"
