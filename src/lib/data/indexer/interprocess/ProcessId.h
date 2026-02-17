#ifndef PROCESS_ID_H
#define PROCESS_ID_H

#include <cstddef>

#include "utilityEnum.h"

enum class ProcessId : std::size_t
{
	NONE = 0,

	INVALID = MAX_ENUM_VALUE<ProcessId>
};

#endif // PROCESS_ID_H
