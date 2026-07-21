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
#include "TimeStamp.h"
