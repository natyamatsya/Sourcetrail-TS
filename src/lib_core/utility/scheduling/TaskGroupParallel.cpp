#include "TaskGroupParallel.h"

#include <chrono>

#include "Strand.h"
#include "TaskScheduler.h"
#include "WorkPools.h"
#include "logging.h"

namespace
{
// How many updates a child gets in a turn before its strand goes back to the pool
// (a child that never waits must not keep a pool thread to itself).
constexpr int updatesPerTurn = 8;

// How long doTerminate waits for the children to end before it gives up on them.
constexpr std::chrono::seconds terminateLimit {30};
}	 // namespace

struct TaskGroupParallel::Child
{
	explicit Child(std::shared_ptr<Task> task)
		: runner(std::make_shared<TaskRunner>(std::move(task)))
		, strand(execution::Strand::create(execution::workPool()))
	{
	}

	const std::shared_ptr<TaskRunner> runner;
	const std::shared_ptr<execution::Strand> strand;
	std::shared_ptr<Blackboard> blackboard;	// the group's, set as it starts
	std::atomic<bool> ended = true;	// not started, or done
};

TaskGroupParallel::TaskGroupParallel() = default;

void TaskGroupParallel::addTask(std::shared_ptr<Task> task)
{
	m_shared->children.push_back(std::make_shared<Child>(std::move(task)));
}

void TaskGroupParallel::doEnter(std::shared_ptr<Blackboard> blackboard)
{
	m_shared->failed = false;

	if (!m_started)
	{
		m_started = true;
		for (const std::shared_ptr<Child>& child: m_shared->children)
		{
			child->blackboard = blackboard;
			start(m_shared, child);
		}
	}
}

Task::TaskState TaskGroupParallel::doUpdate(std::shared_ptr<Blackboard>  /*blackboard*/)
{
	using enum Task::TaskState;
	// Parked until the last child ended, holding no thread of its scheduler.
	const std::shared_ptr<Shared>& shared = m_shared;
	if (!shared->childEnded.waitUntil([&shared]() {
			const std::lock_guard<std::mutex> lock(shared->mutex);
			return shared->running == 0;
		}))
	{
		return STATE_RUNNING;
	}

	return (shared->failed ? STATE_FAILURE : STATE_SUCCESS);
}

void TaskGroupParallel::doExit(std::shared_ptr<Blackboard>  /*blackboard*/) {}

void TaskGroupParallel::doReset(std::shared_ptr<Blackboard> blackboard)
{
	for (const std::shared_ptr<Child>& child: m_shared->children)
	{
		child->runner->reset();
		if (child->ended)
		{
			child->blackboard = blackboard;
			start(m_shared, child);
		}
	}
}

void TaskGroupParallel::doTerminate()
{
	for (const std::shared_ptr<Child>& child: m_shared->children)
	{
		child->runner->terminate();
	}
	// A parked child looks again, and ends.
	for (const std::shared_ptr<Child>& child: m_shared->children)
	{
		if (!child->ended)
		{
			child->runner->unpark();
			child->strand->post([shared = m_shared, child]() { turn(shared, child); });
		}
	}

	std::unique_lock<std::mutex> lock(m_shared->mutex);
	if (!m_shared->allEnded.wait_for(lock, terminateLimit, [this]() { return m_shared->running == 0; }))
	{
		LOG_WARNING(
			"a parallel task group was terminated with " + std::to_string(m_shared->running) +
			" children still running: they are left to end on their own");
	}
}

void TaskGroupParallel::start(const std::shared_ptr<Shared>& shared, const std::shared_ptr<Child>& child)
{
	child->ended = false;
	{
		const std::lock_guard<std::mutex> lock(shared->mutex);
		shared->running++;
	}
	child->strand->post([shared, child]() { turn(shared, child); });
}

void TaskGroupParallel::turn(const std::shared_ptr<Shared>& shared, const std::shared_ptr<Child>& child)
{
	using enum Task::TaskState;
	// Unparks the child and has it look again, in a turn of its own. Weak: what
	// keeps the wake (a timer, a task waiting in the child) must not keep the child.
	const std::function<void()> wake = [weakShared = std::weak_ptr<Shared>(shared),
										weakChild = std::weak_ptr<Child>(child)]() {
		const std::shared_ptr<Child> parked = weakChild.lock();
		const std::shared_ptr<Shared> alive = weakShared.lock();
		if (parked && alive)
		{
			parked->runner->unpark();
			parked->strand->post([alive, parked]() { turn(alive, parked); });
		}
	};

	for (int update = 0; update < updatesPerTurn; update++)
	{
		if (child->ended)
		{
			return;	// a turn a wake posted after it ended
		}

		const TaskState state = TaskScheduler::updateDriven(child->runner, wake, child->blackboard);
		if (state == STATE_SUCCESS || state == STATE_FAILURE)
		{
			if (state == STATE_FAILURE)
			{
				shared->failed = true;
			}
			child->ended = true;
			{
				const std::lock_guard<std::mutex> lock(shared->mutex);
				shared->running--;
			}
			shared->allEnded.notify_all();
			shared->childEnded.notify();
			return;
		}
		if (child->runner->isParked())
		{
			return;	// `wake` posts its next turn
		}
	}
	// It runs on: the rest in a turn posted anew, after what else waits for the pool.
	child->strand->post([shared, child]() { turn(shared, child); });
}
