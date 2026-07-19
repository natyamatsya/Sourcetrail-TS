// `srctrl.utility:cache` partition -- the cache helpers. Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <functional>
#include <map>
#include <unordered_map>
#endif

export module srctrl.utility:cache;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

#define SRCTRL_MODULE_PURVIEW
#include "SingleValueCache.h"
#include "OrderedCache.h"
#include "UnorderedCache.h"
