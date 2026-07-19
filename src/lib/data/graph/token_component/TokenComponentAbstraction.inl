// Inline member definitions for TokenComponentAbstraction.h (included at the end of that header). All
// members are inline because an out-of-line member of an exported class does not resolve for module
// importers.

#pragma once

inline TokenComponentAbstraction::TokenComponentAbstraction(AbstractionType abstraction)
	: m_abstraction(abstraction)
{
}

inline TokenComponentAbstraction::~TokenComponentAbstraction() = default;

inline std::shared_ptr<TokenComponent> TokenComponentAbstraction::copy() const
{
	return std::make_shared<TokenComponentAbstraction>(*this);
}

inline TokenComponentAbstraction::AbstractionType TokenComponentAbstraction::getAbstraction() const
{
	return m_abstraction;
}

inline std::string TokenComponentAbstraction::getAbstractionString() const
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
