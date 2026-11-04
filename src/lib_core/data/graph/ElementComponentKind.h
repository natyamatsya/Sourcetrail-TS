#ifndef ELEMENT_COMPONENT_KIND_H
#define ELEMENT_COMPONENT_KIND_H

#include "SrctrlModule.h"

// utilityEnum (intToEnum / lookupEnum) lives in srctrl.utility. In the header build we #include it; in
// the module build the wrapper `import srctrl.utility;`s before pulling us into the purview, so we must
// NOT re-include (and re-export) it here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityEnum.h"
#endif

SRCTRL_EXPORT enum class ElementComponentKind
{
	ELEMENT_COMPONENT_NONE = 0,
	ELEMENT_COMPONENT_IS_AMBIGUOUS = 1,
	// On a CALL edge: whether its calls dispatch at run time, as the indexer that recorded
	// them knows it (the C++ indexer, from clang). DISPATCHES when at least one call behind the
	// edge does -- an edge is the union of its calls, from every translation unit -- STATIC
	// when the indexer saw only calls bound at compile time. An edge with neither was recorded
	// by an indexer that does not tell. See context/DESIGN_DISPATCH_EDGES.md.
	ELEMENT_COMPONENT_CALL_DISPATCHES = 2,
	ELEMENT_COMPONENT_CALL_STATIC = 3
};

// An explicit specialization of the imported `intToEnum` template (declared+defined inline in the
// .inl). Explicit specializations aren't separately `export`ed -- they're found via the primary.
template <>
ElementComponentKind intToEnum(int value);

#include "ElementComponentKind.inl"

#endif	  // ELEMENT_COMPONENT_KIND_H
