// Inline implementations for SourceGroupStatusType.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include "logging.h"
#endif

inline std::string sourceGroupStatusTypeToString(SourceGroupStatusType v)
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

inline SourceGroupStatusType stringToSourceGroupStatusType(const std::string &v)
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
