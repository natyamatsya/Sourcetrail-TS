#ifndef STATUS_H
#define STATUS_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#endif

SRCTRL_EXPORT enum class StatusType
{
	STATUS_INFO = 1,
	STATUS_ERROR = 2,
};

SRCTRL_EXPORT typedef int StatusFilter;

SRCTRL_EXPORT struct Status
{
	Status(std::string message, bool isError = false)
		: message(message), type(isError ? StatusType::STATUS_ERROR : StatusType::STATUS_INFO)
	{
	}

	std::string message;
	StatusType type;
};

#endif	  // STATUS_H
