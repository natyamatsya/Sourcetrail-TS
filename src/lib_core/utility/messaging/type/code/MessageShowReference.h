#ifndef MESSAGE_SHOW_REFERENCE_H
#define MESSAGE_SHOW_REFERENCE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#include "types.h"
#endif

SRCTRL_EXPORT class MessageShowReference: public Message<MessageShowReference>
{
public:
	MessageShowReference(size_t refIndex, Id tokenId, Id locationId, bool fromUser)
		: refIndex(refIndex), tokenId(tokenId), locationId(locationId), fromUser(fromUser)
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageShowReference";
	}

	void print(std::ostream& os) const override
	{
		os << "index: " << refIndex << " token: " << tokenId << " location: " << locationId;
	}

	const size_t refIndex;
	const Id tokenId;
	const Id locationId;
	const bool fromUser;
};

#endif	  // MESSAGE_SHOW_REFERENCE_H
