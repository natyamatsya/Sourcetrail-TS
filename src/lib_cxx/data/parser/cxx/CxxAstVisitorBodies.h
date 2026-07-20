#ifndef CXX_AST_VISITOR_BODIES_H
#define CXX_AST_VISITOR_BODIES_H

// Classic-build companion to the visitor blob (CxxAstVisitor + components + the mid-level indexing
// layer, one strongly-connected family: the components' bodies call back into the visitor, whose
// tuple holds them by value). Included ONLY from the bottom of CxxAstVisitor.h -- the family apex,
// whose top includes every blob header -- at which point every class definition the bodies need is
// complete. The interior headers (CxxAstVisitorComponent, the recorders, CxxSymbolRegistry) are
// top-included by other blob headers and therefore must NOT bottom-include the apex themselves;
// every other blob header converges here via its apex bottom-include. The module build never
// enters (srctrl_cxx-visitor.cppm orders headers then .inls explicitly).

#include "CxxAstVisitorComponent.inl"
#include "CxxLocationExtractor.inl"
#include "CxxSymbolRegistry.inl"
#include "CxxConceptReferenceRecorder.inl"
#include "CxxDestructorCallRecorder.inl"
#include "CxxIndexingContext.inl"
#include "CxxAstVisitorComponentContext.inl"
#include "CxxAstVisitorComponentTypeRefKind.inl"
#include "CxxAstVisitorComponentDeclRefKind.inl"
#include "CxxAstVisitorComponentImplicitCode.inl"
#include "CxxAstVisitorComponentDeclarationIndexer.inl"
#include "CxxAstVisitorComponentTypeIndexer.inl"
#include "CxxAstVisitorComponentReferenceIndexer.inl"
#include "CxxAstVisitorComponentModuleIndexer.inl"
#include "CxxAstVisitorComponentBraceRecorder.inl"
#include "CxxAstVisitor.inl"

#endif	  // CXX_AST_VISITOR_BODIES_H
