#ifndef UTILITY_MAIN_H
#define UTILITY_MAIN_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

class NameHierarchy;
#endif

// Functions for handling multiple main functions. (The SearchMatch-side check lives with
// SearchMatch in data/search -- this header is NameHierarchy-only and part of srctrl.data:name.)

SRCTRL_EXPORT bool isMainFunction(const NameHierarchy &nameHierarchy);
SRCTRL_EXPORT void uniquifyMainFunction(NameHierarchy *main, const std::string &uniqueAppendix);
SRCTRL_EXPORT bool isUniquifiedMainFunction(const NameHierarchy &nameHierarchy);
SRCTRL_EXPORT void deuniquifyMainFunction(NameHierarchy *main);

#include "utilityMainFunction.inl"

#endif
