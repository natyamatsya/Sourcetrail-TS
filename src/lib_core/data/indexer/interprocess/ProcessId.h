#ifndef PROCESS_ID_H
#define PROCESS_ID_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstddef>

#include "utilityEnum.h"
#endif

SRCTRL_EXPORT enum class ProcessId : std::size_t
{
	NONE = 0,

	INVALID = MAX_ENUM_VALUE<ProcessId>
};

#endif // PROCESS_ID_H
