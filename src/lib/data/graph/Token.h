#ifndef TOKEN_H
#define TOKEN_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#include <typeinfo>
#include <vector>

#include "LogFacade.h"
#include "TokenComponent.h"
#include "types.h"
#endif

SRCTRL_EXPORT class Token
{
public:
	explicit Token(Id id);
	virtual ~Token();

	Id getId() const;

	virtual bool isNode() const = 0;
	virtual bool isEdge() const = 0;

	const std::vector<Id>& getLocationIds() const;

	void addLocationId(Id locationId);
	void removeLocationId(Id locationId);

	void addComponent(std::shared_ptr<TokenComponent> component);

	template <typename ComponentType>
	ComponentType* getComponent() const;

	template <typename ComponentType>
	std::shared_ptr<ComponentType> removeComponent();

	// Logging.
	virtual std::string getReadableTypeString() const = 0;

protected:
	Token(const Token& other);

	void copyComponentsFrom(const Token& other);

private:
	void operator=(const Token&);

	const Id m_id;	  // own id

	std::vector<Id> m_locationIds;
	std::vector<std::shared_ptr<TokenComponent>> m_components;
};

template <typename ComponentType>
ComponentType* Token::getComponent() const
{
	for (const std::shared_ptr<TokenComponent>& component: m_components)
	{
		TokenComponent* componentPtr = component.get();
		if (typeid(ComponentType) == typeid(*componentPtr))
		{
			return dynamic_cast<ComponentType*>(component.get());
		}
	}
	return nullptr;
}

template <typename ComponentType>
std::shared_ptr<ComponentType> Token::removeComponent()
{
	for (std::size_t i = 0; i < m_components.size(); i++)
	{
		std::shared_ptr<TokenComponent> component = m_components[i];
		TokenComponent* componentPtr = component.get();
		if (typeid(ComponentType) == typeid(*componentPtr))
		{
			m_components.erase(m_components.begin() + i);
			return std::dynamic_pointer_cast<ComponentType>(component);
		}
	}
	return nullptr;
}

// In a module build the wrapper includes the .inl explicitly AFTER all class defs, so guard it here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "Token.inl"
#endif

#endif	  // TOKEN_H
