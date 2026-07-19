#ifndef DEFINITION_KIND_H
#define DEFINITION_KIND_H

#include "SrctrlModule.h"

// utilityEnum (intToEnum / lookupEnum) lives in srctrl.utility. In the header build we #include it; in
// the module build the wrapper `import srctrl.utility;`s before pulling us into the purview, so we must
// NOT re-include (and re-export) it here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityEnum.h"
#endif

// these values need to be the same as DefinitionKind in Java code
SRCTRL_EXPORT enum class DefinitionKind
{
	NONE = 0,
	IMPLICIT = 1,
	EXPLICIT = 2
};

// An explicit specialization of the imported `intToEnum` template (declared+defined inline in the
// .inl). Explicit specializations aren't separately `export`ed -- they're found via the primary.
template<>
DefinitionKind intToEnum(int definitionKind);

#include "DefinitionKind.inl"

#endif	  // DEFINITION_TYPE_H
