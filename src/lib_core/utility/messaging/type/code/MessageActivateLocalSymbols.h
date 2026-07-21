#ifndef MESSAGE_ACTIVATE_LOCAL_SYMBOLS_H
#define MESSAGE_ACTIVATE_LOCAL_SYMBOLS_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#include "types.h"
#endif

SRCTRL_EXPORT class MessageActivateLocalSymbols: public Message<MessageActivateLocalSymbols>
{
public:
	MessageActivateLocalSymbols(const std::vector<Id>& symbolIds): symbolIds(symbolIds)
	{
		setSchedulerId(TabIds::currentTab());
	}

	void addSymbol(Id symbolId)
	{
		symbolIds.push_back(symbolId);
	}

	static const std::string getStaticType()
	{
		return "MessageActivateLocalSymbols";
	}

	void print(std::ostream& os) const override
	{
		for (const Id& symbolId: symbolIds)
		{
			os << symbolId << " ";
		}
	}

	std::vector<Id> symbolIds;
};

#endif	  // MESSAGE_ACTIVATE_LOCAL_SYMBOLS_H
