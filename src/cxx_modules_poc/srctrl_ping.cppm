// Module wrapper for the srctrl.ping POC (compiled only in a module build). The `module;` and
// `export module` lines are literal -- they must never be guarded by a macro / #ifdef.

module;

// Global module fragment: everything the exported headers need. (Just <string> here; a real module
// lists all of its third-party / std dependencies.)
#include <string>

export module srctrl.ping;

// Pull the first-party declarations into the module purview; SRCTRL_MODULE_PURVIEW tells the header
// its includes are already in the GMF above, and SRCTRL_EXPORT (from the -DSRCTRL_MODULE_BUILD flag)
// exports them.
#define SRCTRL_MODULE_PURVIEW
#include "srctrl_ping.h"
