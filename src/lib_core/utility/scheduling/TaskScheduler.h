#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

#include "Task.h"
#include "TaskRunner.h"

namespace execution
{
class Strand;
}

// Runs tasks one at a time, in order; a task that holds goes to the back and runs
// again. The loop runs on a thread of its own (startSchedulerLoopThreaded), or as
// work on a strand (startSchedulerLoopOn): then it holds no thread while it has no
// tasks, and what else runs on the strand (a pipeline continuing there) is
// serialized with its tasks.
class TaskScheduler: public std::enable_shared_from_this<TaskScheduler>
{
public:
	TaskScheduler(TabId schedulerId);
	~TaskScheduler();

	void pushTask(std::shared_ptr<Task> task);
	void pushNextTask(std::shared_ptr<Task> task);

	void startSchedulerLoopThreaded();
	void startSchedulerLoop();
	//! Runs the loop on `strand`, in passes posted when there are tasks. The
	//! scheduler must be owned by a shared_ptr (TaskManager's are). Stopping waits
	//! for the pass posted, which terminates the tasks left as the threaded loop's
	//! last round does (from the strand itself it does not wait).
	void startSchedulerLoopOn(std::shared_ptr<execution::Strand> strand);
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

	// Strand loop: post a pass unless one is posted; a pass runs each runner queued
	// when it began once (until it is not running), then posts the next while
	// runners are left, so other work on the strand runs in between.
	void postPass();
	void runPass();

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

	std::shared_ptr<execution::Strand> m_strand;	// the strand loop's, set once it starts
	bool m_passPosted = false;	// guarded by m_tasksMutex
	std::condition_variable m_passesEnded;	// m_passPosted went false
	bool m_passProcessedAny = false;	// since the queue was last empty; guarded by m_tasksMutex
};

#endif	  // TASK_SCHEDULER_H
