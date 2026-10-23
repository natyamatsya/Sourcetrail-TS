#ifndef TASK_RUNNER_H
#define TASK_RUNNER_H

#include <atomic>
#include <cstdint>
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
	// Parked while still running (not a background task's hold): nothing queued
	// behind it runs before it ends. Its scheduler's to set, under its lock.
	void setHoldsQueue(bool holds);
	bool holdsQueue() const;

	// What it was scheduled on behalf of (the cause current when it was made): it
	// runs under it, and counts as that request's work until it ends (CauseWork).
	std::uint64_t cause() const;

private:
	std::shared_ptr<Task> m_task;
	bool m_reset = false;

	// Only created by the first TaskRunner in the hierarchy, then passed down.
	std::shared_ptr<Blackboard> m_blackboard;

	std::atomic<bool> m_parked = false;
	bool m_holdsQueue = false;
	const std::uint64_t m_cause;
};

#endif	  // TASK_RUNNER_H
