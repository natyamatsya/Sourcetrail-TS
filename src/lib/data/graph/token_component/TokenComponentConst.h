#ifndef TOKEN_COMPONENT_CONST_H
#define TOKEN_COMPONENT_CONST_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TokenComponent.h"
#endif

SRCTRL_EXPORT class TokenComponentConst: public TokenComponent
{
public:
	std::shared_ptr<TokenComponent> copy() const override;
};

#include "TokenComponentConst.inl"

#endif	  // TOKEN_COMPONENT_CONST_H
