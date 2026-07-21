#ifndef TASK_DECORATOR_H
#define TASK_DECORATOR_H

#include <memory>

#include "Task.h"

class TaskRunner;

class TaskDecorator
	: public Task
	, public std::enable_shared_from_this<TaskDecorator>
{
public:
	TaskDecorator();
	std::shared_ptr<TaskDecorator> addChildTask(std::shared_ptr<Task> child);

	virtual void setTask(std::shared_ptr<Task> task);
	void terminate() override;

protected:
	// Protected, not private-NVI like Task's own hooks: decorator subclasses legitimately EXTEND
	// termination (chain the base forwarding, then add their own step -- see TaskFinally) rather
	// than replace it, and a private virtual would force them to duplicate the base body.
	virtual void doTerminate();

	std::shared_ptr<TaskRunner> m_taskRunner;
};

#endif	  // TASK_DECORATOR_H
