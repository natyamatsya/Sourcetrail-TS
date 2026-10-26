#include "TaskSender.h"

#include "TaskScheduler.h"

namespace execution
{
namespace
{
// How many updates a task gets in a turn before its strand goes back to the pool
// (a task that never waits must not keep a pool thread to itself).
constexpr int updatesPerTurn = 8;
}	 // namespace

TaskDriver::TaskDriver(
	std::shared_ptr<Task> task, std::shared_ptr<Blackboard> blackboard, bool reset, std::uint64_t cause)
	: m_runner(std::make_shared<TaskRunner>(std::move(task)))
	, m_strand(Strand::create(workPool()))
	, m_blackboard(std::move(blackboard))
	, m_cause(cause)
{
	if (reset)
	{
		m_runner->reset();
	}
}

void TaskDriver::arm(Completion completion)
{
	m_completion = std::move(completion);
}

void TaskDriver::begin()
{
	m_strand->post([self = shared_from_this()]() { self->turn(); });
}

void TaskDriver::stop()
{
	m_strand->post([self = shared_from_this()]() {
		if (self->m_finished)
		{
			return;
		}
		const CauseScope cause(self->m_cause);
		self->m_runner->terminate();
		self->finish(std::nullopt);
	});
}

void TaskDriver::turn()
{
	using enum Task::TaskState;
	// Has the task look again, in a turn of its own (which unparks it). Weak: what
	// keeps the wake (a timer, a provider's listener) must not keep the task's driver.
	const std::function<void()> wake = [weak = weak_from_this()]() {
		if (const std::shared_ptr<TaskDriver> driver = weak.lock())
		{
			driver->m_strand->post([driver]() { driver->turn(); });
		}
	};

	if (m_finished)
	{
		return;	// a turn a wake posted after it ended
	}
	m_runner->unpark();	// woken, or not parked
	const CauseScope cause(m_cause);
	for (int update = 0; update < updatesPerTurn; update++)
	{
		const Task::TaskState state = TaskScheduler::updateDriven(m_runner, wake, m_blackboard);
		if (state == STATE_SUCCESS || state == STATE_FAILURE)
		{
			finish(state);
			return;
		}
		if (m_runner->isParked())
		{
			return;	// `wake` posts its next turn
		}
	}
	// It runs on: the rest in a turn posted anew, after what else waits for the pool.
	m_strand->post([self = shared_from_this()]() { self->turn(); });
}

void TaskDriver::finish(std::optional<Task::TaskState> state)
{
	m_finished = true;
	// What the task holds (a database open) goes with it now, not with the last turn.
	m_runner.reset();
	m_blackboard.reset();
	if (Completion completion = std::exchange(m_completion, nullptr))
	{
		completion(state);
	}
}
}	 // namespace execution
