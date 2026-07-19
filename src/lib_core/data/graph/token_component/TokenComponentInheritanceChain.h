#ifndef TOKEN_COMPONENT_INHERITANCE_CHAIN_H
#define TOKEN_COMPONENT_INHERITANCE_CHAIN_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TokenComponent.h"
#endif

SRCTRL_EXPORT class TokenComponentInheritanceChain: public TokenComponent
{
public:
	TokenComponentInheritanceChain(const std::vector<Id>& inheritanceEdgeIds)
		: inheritanceEdgeIds(inheritanceEdgeIds)
	{
	}

	std::shared_ptr<TokenComponent> copy() const override
	{
		return std::make_shared<TokenComponentInheritanceChain>(*this);
	}

	const std::vector<Id> inheritanceEdgeIds;
};

#endif	  // TOKEN_COMPONENT_INHERITANCE_CHAIN_H
