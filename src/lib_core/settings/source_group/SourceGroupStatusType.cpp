#include "SourceGroupStatusType.h"

#include "logging.h"

std::string sourceGroupStatusTypeToString(SourceGroupStatusType v)
{
	switch (v)
	{
	case SourceGroupStatusType::ENABLED:
		return "enabled";
	case SourceGroupStatusType::DISABLED:
		return "disabled";
	}
	LOG_WARNING( "Trying to convert unknown Source Group status type to string, falling back to disabled status.");
	return "disabled";
}

SourceGroupStatusType stringToSourceGroupStatusType(const std::string &v)
{
	if (v == sourceGroupStatusTypeToString(SourceGroupStatusType::ENABLED))
	{
		return SourceGroupStatusType::ENABLED;
	}
	else if (v == sourceGroupStatusTypeToString(SourceGroupStatusType::DISABLED))
	{
		return SourceGroupStatusType::DISABLED;
	}
	LOG_WARNING("Trying to convert unknown string to Source Group status type, falling back to disabled status.");
	return SourceGroupStatusType::DISABLED;
}
