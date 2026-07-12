#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include <deque>
#include <functional>
#include <memory>
#include <mutex>

#include "Task.h"
#include "TaskRunner.h"

class TaskScheduler
{
public:
	TaskScheduler(TabId schedulerId);
	~TaskScheduler();

	void pushTask(std::shared_ptr<Task> task);
	void pushNextTask(std::shared_ptr<Task> task);

	void startSchedulerLoopThreaded();
	void startSchedulerLoop();
	void stopSchedulerLoop();

	bool loopIsRunning() const;
	bool hasTasksQueued() const;

	//! Invoked on the scheduler's loop thread each time the runner queue drains to
	//! empty after running at least one task (a "scheduler idle" edge). Single slot;
	//! pass nullptr to clear. Used by AgentControlController for the act-and-observe
	//! `Settled` barrier — the app dispatches message handlers as tasks here, so this
	//! (not MessageQueue idle) is where a command's fan-out actually completes. The
	//! handler must not push tasks (it runs after the drain loop has broken).
	void setIdleHandler(std::function<void()> handler);

	void terminateRunningTasks();

private:
	void processTasks();

	const TabId m_schedulerId;

	bool m_loopIsRunning = false;
	bool m_threadIsRunning = false;
	bool m_terminateRunningTasks = false;

	std::deque<std::shared_ptr<TaskRunner>> m_taskRunners;

	mutable std::mutex m_tasksMutex;
	mutable std::mutex m_loopMutex;
	mutable std::mutex m_threadMutex;

	// Guarded by m_tasksMutex; copied out under the lock before invocation.
	std::function<void()> m_idleHandler;
};

#endif	  // TASK_SCHEDULER_H
