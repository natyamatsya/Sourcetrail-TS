#ifndef CXX_NAME_RESOLVER_BODIES_H
#define CXX_NAME_RESOLVER_BODIES_H

// Classic-build companion to the name_resolver family (mutually recursive inline bodies). Included
// ONLY from the bottom of CxxDeclNameResolver.h -- the family apex -- at which point every class
// definition the bodies need is complete: the apex's own top pulls CxxNameResolver /
// CxxTypeNameResolver, and the two siblings below finish the set (a guard-skip here can only mean
// that header already passed its class definition, since bottoms run after classes). The module
// build never enters (srctrl_cxx-parser.cppm orders headers then .inls explicitly).

#include "CxxSpecifierNameResolver.h"
#include "CxxTemplateArgumentNameResolver.h"
#include "CxxTemplateParameterStringResolver.h"

#include "CxxNameResolver.inl"
#include "CxxTypeNameResolver.inl"
#include "CxxDeclNameResolver.inl"
#include "CxxSpecifierNameResolver.inl"
#include "CxxTemplateArgumentNameResolver.inl"
#include "CxxTemplateParameterStringResolver.inl"

#endif	  // CXX_NAME_RESOLVER_BODIES_H
