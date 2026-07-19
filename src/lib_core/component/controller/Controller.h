#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "Component.h"
#include "TabIds.h"

class Controller
{
public:
	Controller();
	virtual ~Controller();

	void setComponent(Component* component);

	virtual void clear() = 0;
	
	TabId getTabId() const;

protected:
	template <typename ViewType>
	ViewType* getView() const;

private:
	Component* m_component = nullptr;
};


template <typename ViewType>
ViewType* Controller::getView() const
{
	if (m_component)
	{
		return m_component->getView<ViewType>();
	}

	return nullptr;
}

#endif	  // CONTROLLER_H
