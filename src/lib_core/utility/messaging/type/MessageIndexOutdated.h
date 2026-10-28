#ifndef MESSAGE_INDEX_OUTDATED_H
#define MESSAGE_INDEX_OUTDATED_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

// A check found this many indexed files changed on disk since they were indexed
// (IndexFreshness has them); 0 when the index is up to date.
SRCTRL_EXPORT class MessageIndexOutdated: public Message<MessageIndexOutdated>
{
public:
	explicit MessageIndexOutdated(std::size_t changedFileCount): changedFileCount(changedFileCount) {}

	static const std::string getStaticType()
	{
		return "MessageIndexOutdated";
	}

	void print(std::ostream& os) const override
	{
		os << changedFileCount;
	}

	const std::size_t changedFileCount;
};

#endif	  // MESSAGE_INDEX_OUTDATED_H
