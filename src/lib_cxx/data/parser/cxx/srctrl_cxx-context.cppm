// `srctrl.cxx:context` partition -- CxxContext, the tagged decl-or-type pointer of the AST traversal.
// Module build only.
//
// Deliberately the FIRST Clang-facing partition: the purview is a single using-alias, so this unit
// isolates and measures the Clang-header GMF/BMI cost (clang/AST/Decl.h + Type.h serialized into the
// full, non-reduced BMI) before the real parser layers commit to it. The alias is attached to the
// module; the clang types it points at stay global-module, so classic TUs and module consumers see
// the same clang entities.

module;

#include <llvm/ADT/PointerUnion.h>

#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>

export module srctrl.cxx:context;

#define SRCTRL_MODULE_PURVIEW
#include "CxxContext.h"
