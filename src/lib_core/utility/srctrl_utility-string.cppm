// `srctrl.utility:string` partition -- the utilityString helpers. Module build only.
// The 4 Qt-backed locale functions are inline in utilityString.inl (their <QString> comes from this
// GMF): module-attached declarations with classic out-of-line definitions do not link for importers.

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

// Qt is never covered by `import std` -- stays textual in both import-std modes.
#include <QString>

export module srctrl.utility:string;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

#define SRCTRL_MODULE_PURVIEW
#include "utilityString.h"
