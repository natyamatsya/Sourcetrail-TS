#ifndef TOKEN_COMPONENT_STATIC_H
#define TOKEN_COMPONENT_STATIC_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TokenComponent.h"
#endif

SRCTRL_EXPORT class TokenComponentStatic: public TokenComponent
{
public:
	std::shared_ptr<TokenComponent> copy() const override;
};

#include "TokenComponentStatic.inl"

#endif	  // TOKEN_COMPONENT_STATIC_H
