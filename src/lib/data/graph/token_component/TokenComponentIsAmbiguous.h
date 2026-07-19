#ifndef TOKEN_COMPONENT_IS_AMBIGUOUS_H
#define TOKEN_COMPONENT_IS_AMBIGUOUS_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TokenComponent.h"
#endif

SRCTRL_EXPORT class TokenComponentIsAmbiguous: public TokenComponent
{
public:
	inline std::shared_ptr<TokenComponent> copy() const override
	{
		return std::make_shared<TokenComponentIsAmbiguous>(*this);
	}
};

#endif	  // TOKEN_COMPONENT_IS_AMBIGUOUS_H
