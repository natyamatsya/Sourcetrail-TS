#ifndef TOKEN_COMPONENT_ABSTRACTION_H
#define TOKEN_COMPONENT_ABSTRACTION_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "TokenComponent.h"
#endif

SRCTRL_EXPORT class TokenComponentAbstraction: public TokenComponent
{
public:
	enum class AbstractionType
	{
		ABSTRACTION_VIRTUAL,
		ABSTRACTION_PURE_VIRTUAL,
		ABSTRACTION_NONE
	};

	TokenComponentAbstraction(AbstractionType abstraction);
	~TokenComponentAbstraction() override;

	std::shared_ptr<TokenComponent> copy() const override;

	AbstractionType getAbstraction() const;
	std::string getAbstractionString() const;

private:
	const AbstractionType m_abstraction;
};

#include "TokenComponentAbstraction.inl"

#endif	  // TOKEN_COMPONENT_ABSTRACTION_H
