// `srctrl.cxx:name` partition -- the C++ name models (CxxName type-erased wrapper + concept, the
// CxxDeclName/CxxFunctionDeclName/CxxStaticFunctionDeclName/CxxVariableDeclName/CxxTypeName leaves,
// CxxQualifierFlags). Phase 3's first slice: pure value types with no Clang dependency, so the
// module bootstraps without paying the Clang-header BMI cost. Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <concepts>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#endif

// Non-modularized GMF dep: the P2988 optional<T&> shim (third-party, header-only).
#include <stdcompat/optional>

export module srctrl.cxx:name;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // utilityEnum (CxxQualifierFlags' flag operators), utilityString (join)
import srctrl.data;      // NameHierarchy / NameElement / NameDelimiterType (:name)

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// Dependency order: qualifier flags and the erased wrapper first, then the leaves (their guarded
// #includes of each other are skipped in the purview).
#include "CxxQualifierFlags.h"
#include "CxxName.h"
#include "CxxDeclName.h"
#include "CxxTypeName.h"
#include "CxxFunctionDeclName.h"
#include "CxxStaticFunctionDeclName.h"
#include "CxxVariableDeclName.h"

// close the purview-wide extern "C++" linkage block
}
