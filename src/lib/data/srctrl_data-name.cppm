// `srctrl.data:name` partition -- NameElement + NameHierarchy. Module build only.
// (NameHierarchy::deserialize is out-of-line in NameHierarchy.cpp -- it needs logging.h +
// utilityMainFunction, the latter forward-declaring NameHierarchy, so it can't be in this GMF.)

module;

#include <sstream>
#include <string>
#include <vector>

export module srctrl.data:name;

import srctrl.utility;   // utilityString (NameElement::Signature::getParameterString)
import :types;           // NameDelimiterType (NameHierarchy)

#define SRCTRL_MODULE_PURVIEW
#include "NameElement.h"
#include "NameHierarchy.h"
