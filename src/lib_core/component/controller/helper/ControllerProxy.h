#ifndef CONTROLLER_PROXY_H
#define CONTROLLER_PROXY_H

#include <functional>

#include "TaskLambda.h"

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
			Task::dispatch(m_schedulerId, std::make_shared<TaskLambda>([=] { callback(controller); }));
		}
	}

	template <typename FuncType, typename... Args>
	void executeAsTaskWithArgs(FuncType callback, const Args... args)
	{
		ControllerType* controller = m_view->getController<ControllerType>();
		if (controller)
		{
			Task::dispatch(m_schedulerId, std::make_shared<TaskLambda>([=] { (controller->*callback)(args...); }));
		}
	}

private:
	View* m_view;
	TabId m_schedulerId;
};

#endif	  // CONTROLLER_PROXY_H
