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
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "utilityString.h"

// close the purview-wide extern "C++" linkage block
}
