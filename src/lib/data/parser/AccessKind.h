#ifndef ACCESS_KIND_H
#define ACCESS_KIND_H

#include "utilityEnum.h"

#include <string>

// Persisted to the SQLite DB and the IPC wire as the raw int, so values are
// append-only — never renumber. PACKAGE (Swift's `package` access level) was
// added after TYPE_PARAMETER for that reason.
enum class AccessKind
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

template <>
AccessKind intToEnum(int value);

std::string accessKindToString(AccessKind t);

#endif	  // ACCESS_KIND_H
