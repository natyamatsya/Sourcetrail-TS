#ifndef TOKEN_COMPONENT_H
#define TOKEN_COMPONENT_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>
#endif

SRCTRL_EXPORT class TokenComponent
{
public:
	virtual ~TokenComponent();
	virtual std::shared_ptr<TokenComponent> copy() const = 0;
};

#include "TokenComponent.inl"

#endif	  // TOKEN_COMPONENT_H
