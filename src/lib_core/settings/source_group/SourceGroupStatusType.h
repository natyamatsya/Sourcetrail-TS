#ifndef SOURCE_GROUP_STATUS_TYPE_H
#define SOURCE_GROUP_STATUS_TYPE_H

#include <string>

enum class SourceGroupStatusType
{
	ENABLED,
	DISABLED
};

std::string sourceGroupStatusTypeToString(SourceGroupStatusType v);
SourceGroupStatusType stringToSourceGroupStatusType(const std::string &v);

#endif	  // SOURCE_GROUP_STATUS_TYPE_H
