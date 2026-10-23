// Module build: LOG_* macros stay textual (macros don't travel through imports); logging.h then
// yields macros only and the backend comes from `import srctrl.logging` below.
#ifdef SRCTRL_MODULE_BUILD
#define SRCTRL_LOGGING_VIA_IMPORT
#endif

#include "TaskScheduler.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "MessageCause.h"
#include "ScopedFunctor.h"
#include "Strand.h"
#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.logging;
#endif

namespace
{
// The scheduler and runner whose task the calling thread updates now, if any.
struct CurrentTask
{
	TaskScheduler* scheduler = nullptr;
	std::shared_ptr<TaskRunner> runner;
};
thread_local CurrentTask* t_currentTask = nullptr;
}	 // namespace

TaskScheduler::TaskScheduler(TabId schedulerId)
	: m_schedulerId(schedulerId)
{
}

TaskScheduler::~TaskScheduler()
{
	stopSchedulerLoop();
}

void TaskScheduler::pushTask(std::shared_ptr<Task> task)
{
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		m_taskRunners.push_back(std::make_shared<TaskRunner>(task));
		CauseWork::begun(m_taskRunners.back()->cause());
	}
	postPass();
}

void TaskScheduler::pushNextTask(std::shared_ptr<Task> task)
{
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);

		const auto runner = std::make_shared<TaskRunner>(task);
		CauseWork::begun(runner->cause());
		if (m_taskRunners.size() == 0)
		{
			m_taskRunners.push_front(runner);
		}
		else
		{
			m_taskRunners.insert(m_taskRunners.begin() + 1, runner);
		}
	}
	postPass();
}

void TaskScheduler::startSchedulerLoopThreaded()
{
	std::thread(&TaskScheduler::startSchedulerLoop, this).detach();

	std::lock_guard<std::mutex> lock(m_threadMutex);
	m_threadIsRunning = true;
}

void TaskScheduler::startSchedulerLoop()
{
	using enum Task::TaskState;
	{
		std::lock_guard<std::mutex> lock(m_loopMutex);

		if (m_loopIsRunning)
		{
			LOG_ERROR("Unable to start task scheduler. Loop is already running.");
			return;
		}

		m_loopIsRunning = true;
	}

	while (true)
	{
		processTasks();

		{
			std::lock_guard<std::mutex> lock(m_loopMutex);

			if (!m_loopIsRunning)
			{
				break;
			}
		}

		std::this_thread::sleep_for(threadLoopInterval);
	}

	{
		std::lock_guard<std::mutex> lock(m_threadMutex);
		if (m_threadIsRunning)
		{
			m_threadIsRunning = false;
		}
	}
}

void TaskScheduler::startSchedulerLoopOn(std::shared_ptr<execution::Strand> strand)
{
	{
		std::lock_guard<std::mutex> lock(m_loopMutex);

		if (m_loopIsRunning)
		{
			LOG_ERROR("Unable to start task scheduler. Loop is already running.");
			return;
		}

		m_loopIsRunning = true;
	}
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		m_strand = std::move(strand);
	}
	postPass();	// the tasks pushed before it started
}

void TaskScheduler::stopSchedulerLoop()
{
	using enum Task::TaskState;
	{
		std::lock_guard<std::mutex> lock(m_loopMutex);

		if (!m_loopIsRunning)
		{
			LOG_WARNING("Unable to stop task scheduler. Loop is not running.");
		}

		m_loopIsRunning = false;
	}
	{
		std::unique_lock<std::mutex> lock(m_tasksMutex);
		if (m_strand)
		{
			// A last pass terminates what is left, parked runners too (an indexing run
			// waiting for its indexers): nothing else would post one for them.
			for (const std::shared_ptr<TaskRunner>& runner: m_taskRunners)
			{
				runner->unpark();
				runner->setHoldsQueue(false);
			}
			if (!m_passPosted && !m_taskRunners.empty())
			{
				m_passPosted = true;
				m_strand->post([self = shared_from_this()]() { self->runPass(); });
			}
			if (!m_strand->runsHere())
			{
				m_passesEnded.wait(lock, [this]() { return !m_passPosted; });
			}
			return;
		}
	}

	while (true)
	{
		{
			std::lock_guard<std::mutex> lock(m_threadMutex);
			if (!m_threadIsRunning)
			{
				break;
			}
		}

		std::this_thread::sleep_for(threadLoopInterval);
	}
}

bool TaskScheduler::loopIsRunning() const
{
	std::lock_guard<std::mutex> lock(m_loopMutex);
	return m_loopIsRunning;
}

bool TaskScheduler::hasTasksQueued() const
{
	std::lock_guard<std::mutex> lock(m_tasksMutex);
	return m_taskRunners.size();
}

void TaskScheduler::terminateRunningTasks()
{
	m_terminateRunningTasks = true;
}

void TaskScheduler::processTasks()
{
	using enum Task::TaskState;

	std::lock_guard<std::mutex> lock(m_tasksMutex);

	// Parked runners are looked at again each loop.
	for (const std::shared_ptr<TaskRunner>& runner: m_taskRunners)
	{
		runner->unpark();
	}

	while (hasRunnable())
	{
		runFront();
	}

	m_terminateRunningTasks = false;
}

void TaskScheduler::postPass()
{
	std::shared_ptr<execution::Strand> strand;
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		if (!m_strand || m_passPosted || m_taskRunners.empty())
		{
			return;
		}
		m_passPosted = true;
		strand = m_strand;
	}
	strand->post([self = shared_from_this()]() { self->runPass(); });
}

void TaskScheduler::runPass()
{
	using enum Task::TaskState;

	std::shared_ptr<execution::Strand> strand;
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);

		for (std::size_t round = m_taskRunners.size(); round > 0 && hasRunnable(); round--)
		{
			runFront();
		}

		// While nothing may run, no pass is posted: waking a parked runner posts it.
		if (hasRunnable())
		{
			strand = m_strand;	// the next pass, after what else waits on the strand
		}
		else
		{
			m_passPosted = false;
			m_terminateRunningTasks = false;
		}
	}

	if (strand)
	{
		strand->post([self = shared_from_this()]() { self->runPass(); });
	}
	else
	{
		m_passesEnded.notify_all();
	}
}

bool TaskScheduler::hasRunnable() const
{
	for (const std::shared_ptr<TaskRunner>& runner: m_taskRunners)
	{
		if (!runner->isParked())
		{
			return true;
		}
		if (runner->holdsQueue())
		{
			return false;
		}
	}
	return false;
}

void TaskScheduler::runFront()
{
	using enum Task::TaskState;

	const std::shared_ptr<TaskRunner> runner = m_taskRunners.front();
	if (runner->isParked())
	{
		m_taskRunners.pop_front();	// a parked background runner: the others go first
		m_taskRunners.push_back(runner);
		return;
	}

	Task::TaskState state = STATE_FAILURE;
	{
		m_tasksMutex.unlock();

		[[maybe_unused]]
		ScopedFunctor functor([this]()
		{
			m_tasksMutex.lock();
		});

		state = runUntilItWaits(runner);
	}

	runner->setHoldsQueue(state == STATE_RUNNING && runner->isParked());
	if (runner->holdsQueue())
	{
		return;	// stays at the front until it is woken
	}
	m_taskRunners.pop_front();
	if (state == STATE_HOLD)
	{
		m_taskRunners.push_back(runner);
	}
	else
	{
		// Done with, for good. Outside the lock: the work's end calls back (CauseWork).
		m_tasksMutex.unlock();
		CauseWork::ended(runner->cause());
		m_tasksMutex.lock();
	}
}

Task::TaskState TaskScheduler::runUntilItWaits(const std::shared_ptr<TaskRunner>& runner)
{
	using enum Task::TaskState;

	CurrentTask current {this, runner};
	const CauseScope cause(runner->cause());	// what it dispatches carries it on
	CurrentTask* const outer = std::exchange(t_currentTask, &current);
	[[maybe_unused]]
	ScopedFunctor restore([outer]()
	{
		t_currentTask = outer;
	});

	Task::TaskState state = STATE_RUNNING;
	while (true)
	{
		{
			std::lock_guard<std::mutex> loopLock(m_loopMutex);

			if (!m_loopIsRunning || m_terminateRunningTasks)
			{
				runner->terminate();
				return STATE_FAILURE;	// dropped, parked or not
			}
		}

		state = runner->update(m_schedulerId);
		if (state != STATE_RUNNING || runner->isParked())
		{
			return state;
		}
	}
}

std::function<void()> TaskScheduler::parkCurrentTask()
{
	if (t_currentTask == nullptr || !t_currentTask->runner)
	{
		return {};
	}
	t_currentTask->runner->park();
	return [scheduler = t_currentTask->scheduler->weak_from_this(),
			runner = std::weak_ptr<TaskRunner>(t_currentTask->runner)]() {
		const std::shared_ptr<TaskScheduler> owner = scheduler.lock();
		const std::shared_ptr<TaskRunner> parked = runner.lock();
		if (owner && parked)
		{
			owner->wake(*parked);
		}
		else if (parked)
		{
			parked->unpark();	// a scheduler of a scope of its own: its loop looks again
		}
	};
}

void TaskScheduler::waitForNextLook()
{
	std::this_thread::sleep_for(threadLoopInterval);
}

void TaskScheduler::wake(TaskRunner& runner)
{
	std::shared_ptr<execution::Strand> strand;
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);
		runner.unpark();
		if (m_strand && !m_passPosted && !m_taskRunners.empty())
		{
			m_passPosted = true;
			strand = m_strand;
		}
	}
	if (strand)
	{
		strand->post([self = shared_from_this()]() { self->runPass(); });
	}
}
