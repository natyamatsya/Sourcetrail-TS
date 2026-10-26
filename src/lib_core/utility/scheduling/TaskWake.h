#ifndef TASK_WAKE_H
#define TASK_WAKE_H

#include <functional>
#include <mutex>
#include <utility>

#include "TaskParking.h"

// A task that waits for something other threads bring about (its children ending, a
// decision): in its update it asks waitUntil(done); the others call notify() once what
// `done` reads has changed. Until then the task is parked, holding no thread
// (TaskParking::parkCurrentTask), or, where nothing parks it, it waits a look.
class TaskWake
{
public:
	// In the task's update: whether `done()`; else the task waits for notify().
	template <typename Done>
	bool waitUntil(Done done)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		if (done())
		{
			return true;
		}
		m_wake = TaskParking::parkCurrentTask();
		if (!m_wake)
		{
			lock.unlock();
			TaskParking::waitForNextLook();
		}
		return false;
	}

	// From any thread, after what `done` reads has changed.
	void notify()
	{
		std::function<void()> wake;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			wake = std::exchange(m_wake, nullptr);
		}
		if (wake)
		{
			wake();
		}
	}

private:
	std::mutex m_mutex;	// orders done() and parking against notify()
	std::function<void()> m_wake;
};

#endif	  // TASK_WAKE_H
