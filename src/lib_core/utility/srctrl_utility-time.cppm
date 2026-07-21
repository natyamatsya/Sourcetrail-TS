// `srctrl.utility:time` partition -- TimeStamp. Pure std (no Qt). Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#endif

// POSIX localtime_r (TimeStamp.inl) is NOT part of `import std` -- the C header stays textual
// in the import-std build too.
#include <time.h>

export module srctrl.utility:time;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "TimeStamp.h"

// close the purview-wide extern "C++" linkage block
}
