// Module wrapper for AidKit -- compiled only in a module build (a FILE_SET CXX_MODULES source).
// The `module;` / `export module` lines are literal; they may not sit behind an #ifdef.

module;

// Global module fragment: the third-party headers the exported AidKit headers need. Qt is not a
// module, so <QString> always stays here. The std headers are here only when NOT importing std.
#include <QString>
#ifndef SRCTRL_IMPORT_STD
#include <algorithm>
#include <functional>
#include <string_view>
#include <vector>
#include <mutex>
#include <type_traits>
#include <ostream>
#endif

export module aidkit;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

// Pull the first-party headers into the module purview. SRCTRL_MODULE_PURVIEW makes them skip the
// includes above (already in the GMF / provided by `import std`) and turns SRCTRL_EXPORT into `export`.
#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "aidkit/enum_class.hpp"
#include "aidkit/thread_shared.hpp"
#include "aidkit/qt/Strings.hpp"

// close the purview-wide extern "C++" linkage block
}
