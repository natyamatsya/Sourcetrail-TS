#ifndef MESSAGE_ERROR_COUNT_UPDATE_H
#define MESSAGE_ERROR_COUNT_UPDATE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "ErrorCountInfo.h"
#endif

SRCTRL_EXPORT class MessageErrorCountUpdate: public Message<MessageErrorCountUpdate>
{
public:
	static const std::string getStaticType()
	{
		return "MessageErrorCountUpdate";
	}

	MessageErrorCountUpdate(const ErrorCountInfo& errorCount, const std::vector<ErrorInfo>& newErrors)
		: errorCount(errorCount), newErrors(newErrors)
	{
		setSendAsTask(false);
	}

	void print(std::ostream& os) const override
	{
		os << errorCount.total << '/' << errorCount.fatal << " - " << newErrors.size()
		   << " new errors";
	}

	const ErrorCountInfo errorCount;
	std::vector<ErrorInfo> newErrors;
};

#endif	  // MESSAGE_ERROR_COUNT_UPDATE_H
