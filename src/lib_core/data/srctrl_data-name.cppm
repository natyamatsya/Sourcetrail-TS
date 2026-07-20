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
#include "NameElement.h"
#include "NameHierarchy.h"
#include "utilityMainFunction.h"
