// `srctrl.data:name` partition -- NameElement + NameHierarchy + the main-function helpers
// (utilityMainFunction). Module build only. (NameHierarchy::deserialize is out-of-line in
// NameHierarchy.cpp -- it needs the logging macros, so it stays an include-only member, like the
// logging/Qt seams.)

module;

#include <cassert>

#ifndef SRCTRL_IMPORT_STD
#include <sstream>
#include <string>
#include <vector>
#endif

export module srctrl.data:name;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // utilityString (NameElement::Signature::getParameterString)
import :types;           // NameDelimiterType (NameHierarchy)

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "NameElement.h"
#include "NameHierarchy.h"
#include "utilityMainFunction.h"

// close the purview-wide extern "C++" linkage block
}
