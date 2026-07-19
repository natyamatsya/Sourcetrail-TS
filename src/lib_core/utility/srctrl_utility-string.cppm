// `srctrl.utility:string` partition -- the utilityString helpers. Module build only.
// Only the pure-std functions are exported; utilityString's 4 Qt-dependent locale functions stay
// out-of-line in utilityString.cpp and are not part of the module (an include-only seam).

module;

#ifndef SRCTRL_IMPORT_STD
#include <algorithm>
#include <cctype>
#include <deque>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#endif

export module srctrl.utility:string;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

#define SRCTRL_MODULE_PURVIEW
#include "utilityString.h"
