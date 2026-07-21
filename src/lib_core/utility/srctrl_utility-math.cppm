// `srctrl.utility:math` partition -- the Vector2 / VectorBase math templates. Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#endif

export module srctrl.utility:math;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import :types;           // Property (Vector2's named-component base)
import srctrl.logging;   // srctrl::log::error in Vector2's bounds-checked accessors

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// VectorBase first -- Vector2 derives from it.
#include "VectorBase.h"
#include "Vector2.h"

// close the purview-wide extern "C++" linkage block
}
