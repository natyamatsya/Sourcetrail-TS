#ifndef MESSAGE_FOCUS_VIEW_H
#define MESSAGE_FOCUS_VIEW_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageFocusView: public Message<MessageFocusView>
{
public:
	enum class ViewType
	{
		GRAPH,
		CODE,
		TOGGLE
	};

	MessageFocusView(ViewType type): type(type)
	{
		setIsLogged(false);
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageFocusView";
	}

	void print(std::ostream& os) const override
	{
		switch (type)
		{
		case ViewType::GRAPH:
			os << "graph";
			break;
		case ViewType::CODE:
			os << "code";
			break;
		case ViewType::TOGGLE:
			os << "toggle";
			break;
		}
	}

	const ViewType type;
};

#endif	  // MESSAGE_FOCUS_VIEW_H
