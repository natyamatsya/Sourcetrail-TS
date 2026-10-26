#ifndef CONTROLLER_PROXY_H
#define CONTROLLER_PROXY_H

#include <functional>

#include "TabQueues.h"

#include "View.h"

template <typename ControllerType>
class ControllerProxy
{
public:
	ControllerProxy(View* view, TabId schedulerId)
		: m_view(view), m_schedulerId(schedulerId) 
	{}

	void execute(std::function<void(ControllerType*)> callback)
	{
		ControllerType* controller = m_view->getController<ControllerType>();
		if (controller)
		{
			callback(controller);
		}
	}

	void executeAsTask(std::function<void(ControllerType*)> callback)
	{
		ControllerType* controller = m_view->getController<ControllerType>();
		if (controller)
		{
			execution::post(m_schedulerId, [=] { callback(controller); });
		}
	}

	template <typename FuncType, typename... Args>
	void executeAsTaskWithArgs(FuncType callback, const Args... args)
	{
		ControllerType* controller = m_view->getController<ControllerType>();
		if (controller)
		{
			execution::post(m_schedulerId, [=] { (controller->*callback)(args...); });
		}
	}

private:
	View* m_view;
	TabId m_schedulerId;
};

#endif	  // CONTROLLER_PROXY_H
