#ifndef MESSAGE_ACTIVATE_BASE_H
#define MESSAGE_ACTIVATE_BASE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "SearchMatch.h"
#endif

SRCTRL_EXPORT class MessageActivateBase
{
public:
	virtual ~MessageActivateBase() = default;

	virtual std::vector<SearchMatch> getSearchMatches() const = 0;
};

#endif	  // MESSAGE_ACTIVATE_BASE_H
