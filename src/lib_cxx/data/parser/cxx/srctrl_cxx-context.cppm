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
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "CxxContext.h"

// close the purview-wide extern "C++" linkage block
}
