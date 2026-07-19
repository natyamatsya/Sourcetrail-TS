#ifndef ACCESS_KIND_H
#define ACCESS_KIND_H

#include "SrctrlModule.h"

// utilityEnum (intToEnum / lookupEnum) lives in srctrl.utility. In the header build we #include it; in
// the module build the wrapper `import srctrl.utility;`s before pulling us into the purview, so we must
// NOT re-include (and re-export) it here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityEnum.h"

#include <string>
#endif

// Persisted to the SQLite DB and the IPC wire as the raw int, so values are
// append-only — never renumber. PACKAGE (Swift's `package` access level) was
// added after TYPE_PARAMETER for that reason.
SRCTRL_EXPORT enum class AccessKind
{
	NONE = 0,
	PUBLIC = 1,
	PROTECTED = 2,
	PRIVATE = 3,
	DEFAULT = 4,
	TEMPLATE_PARAMETER = 5,
	TYPE_PARAMETER = 6,
	PACKAGE = 7
};

// An explicit specialization of the imported `intToEnum` template (declared+defined inline in the
// .inl). Explicit specializations aren't separately `export`ed -- they're found via the primary.
template <>
AccessKind intToEnum(int value);

SRCTRL_EXPORT std::string accessKindToString(AccessKind t);

#include "AccessKind.inl"

#endif	  // ACCESS_KIND_H
