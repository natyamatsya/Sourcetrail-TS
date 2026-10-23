// Module build: LOG_* macros stay textual (macros don't travel through imports); logging.h then
// yields macros only and the backend comes from `import srctrl.logging` below.
#ifdef SRCTRL_MODULE_BUILD
#define SRCTRL_LOGGING_VIA_IMPORT
#endif

#include "TaskScheduler.h"

#include <algorithm>
#include <chrono>
#include <thread>

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
	}
	postPass();
}

void TaskScheduler::pushNextTask(std::shared_ptr<Task> task)
{
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);

		if (m_taskRunners.size() == 0)
		{
			m_taskRunners.push_front(std::make_shared<TaskRunner>(task));
		}
		else
		{
			m_taskRunners.insert(m_taskRunners.begin() + 1, std::make_shared<TaskRunner>(task));
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

	bool processedAny = false;
	std::function<void()> idleHandler;
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);

		// Parked runners are looked at again each loop.
		for (const std::shared_ptr<TaskRunner>& runner: m_taskRunners)
		{
			runner->unpark();
		}

		while (std::ranges::any_of(m_taskRunners, [](const auto& runner) { return !runner->isParked(); }))
		{
			std::shared_ptr<TaskRunner> runner = m_taskRunners.front();
			Task::TaskState state = STATE_HOLD;

			if (!runner->isParked())
			{
				processedAny = true;
				m_tasksMutex.unlock();

				[[maybe_unused]]
				ScopedFunctor functor([this]()
				{
					m_tasksMutex.lock();
				});

				state = runUntilItWaits(runner);
			}

			m_taskRunners.pop_front();

			if (state == STATE_HOLD || (state == STATE_RUNNING && runner->isParked()))
			{
				m_taskRunners.push_back(runner);
			}
		}

		m_terminateRunningTasks = false;

		// Scheduler-idle edge: the runner queue drained after doing work. Copy the
		// handler under the lock; invoke it below, outside the lock (it may publish
		// an event). Runs on this scheduler thread — the same one the message
		// handlers ran on — so the handler sees their effects and shares their
		// thread. It must not push tasks (the drain loop has already broken).
		if (processedAny && m_taskRunners.empty())
		{
			idleHandler = m_idleHandler;
		}
	}

	if (idleHandler)
	{
		idleHandler();
	}
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

	std::function<void()> idleHandler;
	std::shared_ptr<execution::Strand> strand;
	{
		std::lock_guard<std::mutex> lock(m_tasksMutex);

		for (std::size_t round = m_taskRunners.size(); round > 0 && !m_taskRunners.empty(); round--)
		{
			std::shared_ptr<TaskRunner> runner = m_taskRunners.front();
			Task::TaskState state = STATE_HOLD;

			if (!runner->isParked())
			{
				m_passProcessedAny = true;
				m_tasksMutex.unlock();

				[[maybe_unused]]
				ScopedFunctor functor([this]()
				{
					m_tasksMutex.lock();
				});

				state = runUntilItWaits(runner);
			}

			m_taskRunners.pop_front();

			if (state == STATE_HOLD || (state == STATE_RUNNING && runner->isParked()))
			{
				m_taskRunners.push_back(runner);
			}
		}

		// While only parked runners are left, no pass is posted: waking one posts it.
		if (std::ranges::any_of(m_taskRunners, [](const auto& runner) { return !runner->isParked(); }))
		{
			strand = m_strand;	// the next pass, after what else waits on the strand
		}
		else
		{
			m_passPosted = false;
			m_terminateRunningTasks = false;
			// Scheduler-idle edge, as in processTasks (not while parked ones wait).
			if (m_taskRunners.empty() && std::exchange(m_passProcessedAny, false))
			{
				idleHandler = m_idleHandler;
			}
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
	if (idleHandler)
	{
		idleHandler();
	}
}

Task::TaskState TaskScheduler::runUntilItWaits(const std::shared_ptr<TaskRunner>& runner)
{
	using enum Task::TaskState;

	CurrentTask current {this, runner};
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

void TaskScheduler::setIdleHandler(std::function<void()> handler)
{
	std::lock_guard<std::mutex> lock(m_tasksMutex);
	m_idleHandler = std::move(handler);
}
