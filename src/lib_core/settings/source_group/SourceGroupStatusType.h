#ifndef SOURCE_GROUP_STATUS_TYPE_H
#define SOURCE_GROUP_STATUS_TYPE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#endif

SRCTRL_EXPORT enum class SourceGroupStatusType
{
	ENABLED,
	DISABLED
};

SRCTRL_EXPORT std::string sourceGroupStatusTypeToString(SourceGroupStatusType v);
SRCTRL_EXPORT SourceGroupStatusType stringToSourceGroupStatusType(const std::string &v);

#include "SourceGroupStatusType.inl"

#endif	  // SOURCE_GROUP_STATUS_TYPE_H
