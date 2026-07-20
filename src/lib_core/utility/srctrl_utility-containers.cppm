// `srctrl.utility:containers` partition -- the utility.h container/algorithm helper templates
// (concat/append/convert/toVector/toSet/...). Module build only.
//
// utility.h became FilePath-free for this (the toStrings<FilePath> specialization moved to
// utilityFilePath.h, a classic-build seam) -- FilePath here would cycle srctrl.utility <->
// srctrl.file. That also makes utility.h safe in any module unit's GMF.

module;

#ifndef SRCTRL_IMPORT_STD
#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>
#endif

export module srctrl.utility:containers;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

#define SRCTRL_MODULE_PURVIEW
#include "utility.h"
