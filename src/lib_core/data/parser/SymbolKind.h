#ifndef SYMBOL_KIND_H
#define SYMBOL_KIND_H

#include "SrctrlModule.h"

// utilityEnum (intToEnum / lookupEnum) lives in srctrl.utility. In the header build we #include it; in
// the module build the wrapper `import srctrl.utility;`s before pulling us into the purview, so we must
// NOT re-include (and re-export) it here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityEnum.h"
#endif

// These values need to be the same as SymbolKind in Java code (see SymbolKind.java)
SRCTRL_EXPORT enum class SymbolKind
{	 
	UNDEFINED = 0,
	
	ANNOTATION = 1,
	BUILTIN_TYPE = 2,
	CLASS = 3,
	ENUM = 4,
	ENUM_CONSTANT = 5,
	FIELD = 6,
	FUNCTION = 7,
	GLOBAL_VARIABLE = 8,
	INTERFACE = 9,
	MACRO = 10,
	METHOD = 11,
	MODULE = 12,
	NAMESPACE = 13,
	PACKAGE = 14,
	STRUCT = 15,
	TYPEDEF = 16,
	TYPE_PARAMETER = 17,
	UNION = 18,
	RECORD = 19,
	CONCEPT = 20
};

// An explicit specialization of the imported `intToEnum` template (declared+defined inline in the
// .inl). Explicit specializations aren't separately `export`ed -- they're found via the primary.
template <>
SymbolKind intToEnum(int value);

#include "SymbolKind.inl"

#endif	  // SYMBOL_KIND_H
