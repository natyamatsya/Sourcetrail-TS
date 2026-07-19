#ifndef ERROR_COUNT_INFO_H
#define ERROR_COUNT_INFO_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "ErrorInfo.h"
#endif

SRCTRL_EXPORT struct ErrorCountInfo
{
	ErrorCountInfo(): total(0), fatal(0) {}

	ErrorCountInfo(std::size_t total, std::size_t fatal): total(total), fatal(fatal) {}

	ErrorCountInfo(const std::vector<ErrorInfo>& errors): total(0), fatal(0)
	{
		for (const ErrorInfo& error: errors)
		{
			total++;

			if (error.fatal)
			{
				fatal++;
			}
		}
	}

	std::size_t total;
	std::size_t fatal;
};

#endif	  // ERROR_COUNT_INFO_H
