// Inline implementations for MessageStatus.h (included at its end). All definitions inline: the
// messaging family is module-attached in the module build, and inline keeps ordinary mangling so
// classic TUs and the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Cross-module dep: the wrapper supplies utilityString via `import srctrl.utility`.
#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityString.h"
#endif

inline MessageStatus::MessageStatus(
	const std::string& status, bool isError, bool showLoader, bool showInStatusBar)
	: isError(isError), showLoader(showLoader), showInStatusBar(showInStatusBar)
{
	m_stati.push_back(utility::replace(status, "\n", " "));

	setSendAsTask(false);
}

inline MessageStatus::MessageStatus(
	const std::vector<std::string>& stati, bool isError, bool showLoader, bool showInStatusBar)
	: isError(isError), showLoader(showLoader), showInStatusBar(showInStatusBar), m_stati(stati)
{
	setSendAsTask(false);
}

inline const std::string MessageStatus::getStaticType()
{
	return "MessageStatus";
}

inline const std::vector<std::string>& MessageStatus::stati() const
{
	return m_stati;
}

inline std::string MessageStatus::status() const
{
	if (m_stati.size())
	{
		return m_stati[0];
	}

	return "";
}

inline void MessageStatus::print(std::ostream& os) const
{
	for (const std::string& status: m_stati)
	{
		os << status;

		if (m_stati.size() > 1)
		{
			os << " - ";
		}
	}

	if (isError)
	{
		os << " - error";
	}

	if (showLoader)
	{
		os << " - loading";
	}
}
