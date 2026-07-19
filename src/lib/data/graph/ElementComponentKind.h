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
	ELEMENT_COMPONENT_IS_AMBIGUOUS = 1
};

// An explicit specialization of the imported `intToEnum` template (declared+defined inline in the
// .inl). Explicit specializations aren't separately `export`ed -- they're found via the primary.
template <>
ElementComponentKind intToEnum(int value);

#include "ElementComponentKind.inl"

#endif	  // ELEMENT_COMPONENT_KIND_H
