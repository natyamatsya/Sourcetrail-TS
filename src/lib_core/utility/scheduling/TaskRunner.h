#ifndef TASK_RUNNER_H
#define TASK_RUNNER_H

#include <atomic>
#include <memory>

#include "Task.h"

class TaskRunner
{
public:
	TaskRunner(std::shared_ptr<Task> task);
	
	Task::TaskState update(TabId schedulerId);
	Task::TaskState update(std::shared_ptr<Blackboard> blackboard);
	void reset();
	void terminate();	 // caution: this should only be called just before quitting the app.

	// Parked: its scheduler leaves it out until it is woken (TaskScheduler::parkCurrentTask).
	void park();
	void unpark();
	bool isParked() const;

private:
	std::shared_ptr<Task> m_task;
	bool m_reset = false;

	// Only created by the first TaskRunner in the hierarchy, then passed down.
	std::shared_ptr<Blackboard> m_blackboard;

	std::atomic<bool> m_parked = false;
};

#endif	  // TASK_RUNNER_H
