#ifndef LOCATION_TYPE_H
#define LOCATION_TYPE_H

#include "SrctrlModule.h"

// utilityEnum (intToEnum / lookupEnum) lives in srctrl.utility. In the header build we #include it; in
// the module build the srctrl.data:types wrapper `import srctrl.utility;`s before pulling us into the
// purview, so we must NOT re-include (and re-export) it here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityEnum.h"
#endif

SRCTRL_EXPORT enum class LocationType
{
	TOKEN = 0,
	SCOPE = 1,
	QUALIFIER = 2,
	LOCAL_SYMBOL = 3,
	SIGNATURE = 4,
	COMMENT = 5,
	ERROR = 6,
	FULLTEXT_SEARCH = 7,
	SCREEN_SEARCH = 8,
	UNSOLVED = 9
};

// An explicit specialization of the imported `intToEnum` template (declared+defined inline in the
// .inl). Explicit specializations aren't separately `export`ed -- they're found via the primary.
template <>
LocationType intToEnum(int value);

SRCTRL_EXPORT std::size_t operator << (std::size_t bits, LocationType locationType);

#include "LocationType.inl"

#endif	  // LOCATION_TYPE_H
