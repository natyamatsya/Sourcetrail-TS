// Module build: LOG_* macros stay textual (macros don't travel through imports); logging.h then
// yields macros only and the backend comes from `import srctrl.logging` below.
#ifdef SRCTRL_MODULE_BUILD
#define SRCTRL_LOGGING_VIA_IMPORT
#endif

#include "SourceGroupStatusType.h"

#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.logging;
#endif

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
