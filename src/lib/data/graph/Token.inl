// Inline member definitions for Token.h (included at the end of that header). All members are inline
// because an out-of-line member of an exported class does not resolve for module importers. The
// template members getComponent()/removeComponent() are already inline in the header.

#pragma once

inline Token::Token(Id id): m_id(id) {}

inline Token::~Token() = default;

inline Id Token::getId() const
{
	return m_id;
}

inline const std::vector<Id>& Token::getLocationIds() const
{
	return m_locationIds;
}

inline void Token::addLocationId(Id locationId)
{
	m_locationIds.push_back(locationId);
}

inline void Token::removeLocationId(Id locationId)
{
	for (std::vector<Id>::const_iterator it = m_locationIds.begin(); it != m_locationIds.end(); it++)
	{
		if (*it == locationId)
		{
			m_locationIds.erase(it);
			return;
		}
	}

	srctrl::log::error("Location Id was not referenced by this Token.");
}

inline void Token::addComponent(std::shared_ptr<TokenComponent> component)
{
	m_components.push_back(component);
}

inline Token::Token(const Token& other): m_id(other.m_id)
{
	for (const std::shared_ptr<TokenComponent>& component: other.m_components)
	{
		addComponent(component->copy());
	}
}
