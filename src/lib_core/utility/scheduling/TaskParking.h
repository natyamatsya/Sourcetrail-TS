#ifndef TASK_PARKING_H
#define TASK_PARKING_H

#include <chrono>
#include <functional>
#include <memory>

#include "Task.h"
#include "TaskRunner.h"

// How a task in its update waits without holding a thread: it parks, and is woken
// from any thread. What drives the task (execution::runTask) lets it update in
// updateDriven, leaves it alone while it is parked, and goes on once it is woken.
class TaskParking
{
public:
	//! For a task in its update: its runner waits, left alone, until the returned
	//! function is called (from any thread). The task returns STATE_RUNNING. Empty
	//! when nothing drives the calling thread's task (a test updating it itself).
	static std::function<void()> parkCurrentTask();

	//! For a task where nothing parks it (parkCurrentTask was empty): how long it
	//! waits before it looks again.
	static constexpr std::chrono::milliseconds unparkedLookInterval {25};
	static void waitForNextLook();

	//! For a task in its update that wants to look again after `delay` (a poll): its
	//! runner is parked meanwhile and woken by a timer, holding no thread. Where
	//! nothing parks it, the thread sleeps that long. The task returns STATE_RUNNING.
	static void restFor(std::chrono::milliseconds delay);

	//! Updates `runner` with `blackboard` for its driver: a task in it that parks
	//! waits until `wake` is called (from any thread), and `wake` has the driver
	//! unpark the runner and go on.
	static Task::TaskState updateDriven(
		const std::shared_ptr<TaskRunner>& runner,
		std::function<void()> wake,
		const std::shared_ptr<Blackboard>& blackboard);
};

#endif	  // TASK_PARKING_H
