#ifndef TOKEN_COMPONENT_ACCESS_H
#define TOKEN_COMPONENT_ACCESS_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "AccessKind.h"
#include "TokenComponent.h"
#endif

SRCTRL_EXPORT class TokenComponentAccess: public TokenComponent
{
public:
	static std::string getAccessString(AccessKind access);

	TokenComponentAccess(AccessKind access);
	~TokenComponentAccess() override;

	std::shared_ptr<TokenComponent> copy() const override;

	AccessKind getAccess() const;
	std::string getAccessString() const;

private:
	const AccessKind m_access;
};

#include "TokenComponentAccess.inl"

#endif	  // TOKEN_COMPONENT_ACCESS_H
