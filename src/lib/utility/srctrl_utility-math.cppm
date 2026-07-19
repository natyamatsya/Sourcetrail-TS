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
// VectorBase first -- Vector2 derives from it.
#include "VectorBase.h"
#include "Vector2.h"
