// `srctrl.utility:enum` partition -- the utilityEnum helpers. Module build only.
// `module;` / `export module` are literal (never behind an #ifdef).

module;

#include <limits>
#include <ostream>
#include <string>
#include <type_traits>

// NB: the partition name is `enums`, not `enum` -- a partition name can't be a keyword.
export module srctrl.utility:enums;

#define SRCTRL_MODULE_PURVIEW
#include "utilityEnum.h"
