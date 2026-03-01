#include "TokenComponentAbstraction.h"

TokenComponentAbstraction::TokenComponentAbstraction(AbstractionType abstraction)
	: m_abstraction(abstraction)
{
}

TokenComponentAbstraction::~TokenComponentAbstraction() = default;

std::shared_ptr<TokenComponent> TokenComponentAbstraction::copy() const
{
	return std::make_shared<TokenComponentAbstraction>(*this);
}

TokenComponentAbstraction::AbstractionType TokenComponentAbstraction::getAbstraction() const
{
	return m_abstraction;
}

std::string TokenComponentAbstraction::getAbstractionString() const
{
	using enum TokenComponentAbstraction::AbstractionType;
	switch (m_abstraction)
	{
	case ABSTRACTION_VIRTUAL:
		return "virtual";
	case ABSTRACTION_PURE_VIRTUAL:
		return "pure virtual";
	case ABSTRACTION_NONE:
		return "";
	}
	return "";
}
