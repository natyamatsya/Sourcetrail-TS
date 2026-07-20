#ifndef REFERENCE_KIND_H
#define REFERENCE_KIND_H

#include "SrctrlModule.h"

// utilityEnum (intToEnum / lookupEnum) lives in srctrl.utility. In the header build we #include it; in
// the module build the wrapper `import srctrl.utility;`s before pulling us into the purview, so we must
// NOT re-include (and re-export) it here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityEnum.h"
#endif

// These values need to be the same as ReferenceKind in Java code (see ReferenceKind.java)
SRCTRL_EXPORT enum class ReferenceKind
{
	UNDEFINED = 0,
	TYPE_USAGE = 1,
	USAGE = 2,
	CALL = 3,
	INHERITANCE = 4,
	OVERRIDE = 5,
	TYPE_ARGUMENT = 6,
	TEMPLATE_SPECIALIZATION = 7,
	INCLUDE = 8,
	IMPORT = 9,
	MACRO_USAGE = 10,
	ANNOTATION_USAGE = 11
};

// An explicit specialization of the imported `intToEnum` template (declared+defined inline in the
// .inl). Explicit specializations aren't separately `export`ed -- they're found via the primary.
template <>
ReferenceKind intToEnum(int value);

#include "ReferenceKind.inl"

#endif	  // REFERENCE_KIND_H
