#ifndef CXX_CONTEXT_H
#define CXX_CONTEXT_H

#include <llvm/ADT/PointerUnion.h>

#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>

// The decl or type that provides the context of a traversed node — or null when the current
// traversal level records no context. Formerly a small virtual class hierarchy (CxxContext +
// CxxContextDecl/CxxContextType); an llvm::PointerUnion is the idiomatic Clang tagged pointer for
// "exactly one of these", is a single pointer wide, and models the null case natively.
using CxxContext = llvm::PointerUnion<const clang::NamedDecl*, const clang::Type*>;

#endif	  // CXX_CONTEXT_H
