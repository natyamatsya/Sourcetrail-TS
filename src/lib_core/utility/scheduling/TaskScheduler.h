#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include <chrono>
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

	void terminateRunningTasks();

	//! For a task in its update: its runner waits, left out of the scheduler's
	//! passes, until the returned function is called (from any thread). The task
	//! returns STATE_RUNNING or STATE_HOLD as it would anyway. On a thread loop a
	//! parked runner is also looked at again each loop (25 ms). Empty when the
	//! calling thread is not updating a scheduler's task (a parallel group's child):
	//! the task then waits as it did before.
	static std::function<void()> parkCurrentTask();

	//! How often a thread loop looks at its tasks when it has nothing to run.
	static constexpr std::chrono::milliseconds threadLoopInterval {25};
	//! For a task where nothing parks it (parkCurrentTask was empty): waits as long
	//! as a thread loop would before looking at it again.
	static void waitForNextLook();

	//! For a task in its update that wants to look again after `delay` (a poll): its
	//! runner is parked meanwhile and woken by a timer, holding no thread. Where
	//! nothing parks it, the thread sleeps that long. The task returns STATE_RUNNING.
	static void restFor(std::chrono::milliseconds delay);

	//! Updates `runner` with `blackboard` for a driver that is not a scheduler (a
	//! parallel group's child): a task in it that parks waits until `wake` is called
	//! (from any thread), and `wake` unparks the runner and has the driver go on.
	static Task::TaskState updateDriven(
		const std::shared_ptr<TaskRunner>& runner,
		std::function<void()> wake,
		const std::shared_ptr<Blackboard>& blackboard);

private:
	void processTasks();

	// Strand loop: post a pass unless one is posted; a pass runs each runner queued
	// when it began once (until it is not running), then posts the next while
	// runners are left, so other work on the strand runs in between.
	void postPass();
	void runPass();

	// A parked runner can run again.
	void wake(TaskRunner& runner);
	// Under m_tasksMutex: whether a queued runner may run now. A runner parked while
	// running holds the queue, as a running task always did; a parked background
	// runner lets the others go first.
	bool hasRunnable() const;
	// Under m_tasksMutex, when hasRunnable(): runs the front runner (unlocked
	// meanwhile) or, parked, moves it to the back.
	void runFront();
	// Runs `runner` until it is not running, parked or stopped; how it ended
	// (STATE_FAILURE when the loop stopped or tasks were terminated).
	Task::TaskState runUntilItWaits(const std::shared_ptr<TaskRunner>& runner);

	const TabId m_schedulerId;

	bool m_loopIsRunning = false;
	bool m_threadIsRunning = false;
	bool m_terminateRunningTasks = false;

	std::deque<std::shared_ptr<TaskRunner>> m_taskRunners;

	mutable std::mutex m_tasksMutex;
	mutable std::mutex m_loopMutex;
	mutable std::mutex m_threadMutex;

	std::shared_ptr<execution::Strand> m_strand;	// the strand loop's, set once it starts
	bool m_passPosted = false;	// guarded by m_tasksMutex
	std::condition_variable m_passesEnded;	// m_passPosted went false
};

#endif	  // TASK_SCHEDULER_H
