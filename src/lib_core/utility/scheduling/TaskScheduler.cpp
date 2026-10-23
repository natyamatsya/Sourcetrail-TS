// Module build: LOG_* macros stay textual (macros don't travel through imports); logging.h then
// yields macros only and the backend comes from `import srctrl.logging` below.
#ifdef SRCTRL_MODULE_BUILD
#define SRCTRL_LOGGING_VIA_IMPORT
#endif

#include "TaskScheduler.h"

#include <chrono>
#include <thread>

#include "ScopedFunctor.h"
#include "Strand.h"
#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.logging;
#endif

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

		std::this_thread::sleep_for(std::chrono::milliseconds(25));
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

		std::this_thread::sleep_for(std::chrono::milliseconds(25));
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

		while (m_taskRunners.size())
		{
			processedAny = true;
			std::shared_ptr<TaskRunner> runner = m_taskRunners.front();
			Task::TaskState state = STATE_RUNNING;

			{
				m_tasksMutex.unlock();

				[[maybe_unused]]
				ScopedFunctor functor([this]()
				{
					m_tasksMutex.lock();
				});

				while (true)
				{
					{
						std::lock_guard<std::mutex> lock(m_loopMutex);

						if (!m_loopIsRunning || m_terminateRunningTasks)
						{
							runner->terminate();
							break;
						}
					}

					state = runner->update(m_schedulerId);
					if (state != STATE_RUNNING)
					{
						break;
					}
				}
			}

			m_taskRunners.pop_front();

			if (state == STATE_HOLD)
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
		if (processedAny)
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
			m_passProcessedAny = true;
			std::shared_ptr<TaskRunner> runner = m_taskRunners.front();
			Task::TaskState state = STATE_RUNNING;

			{
				m_tasksMutex.unlock();

				[[maybe_unused]]
				ScopedFunctor functor([this]()
				{
					m_tasksMutex.lock();
				});

				while (true)
				{
					{
						std::lock_guard<std::mutex> loopLock(m_loopMutex);

						if (!m_loopIsRunning || m_terminateRunningTasks)
						{
							runner->terminate();
							break;
						}
					}

					state = runner->update(m_schedulerId);
					if (state != STATE_RUNNING)
					{
						break;
					}
				}
			}

			m_taskRunners.pop_front();

			if (state == STATE_HOLD)
			{
				m_taskRunners.push_back(runner);
			}
		}

		if (!m_taskRunners.empty())
		{
			strand = m_strand;	// the next pass, after what else waits on the strand
		}
		else
		{
			m_passPosted = false;
			m_terminateRunningTasks = false;
			// Scheduler-idle edge, as in processTasks.
			if (std::exchange(m_passProcessedAny, false))
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

void TaskScheduler::setIdleHandler(std::function<void()> handler)
{
	std::lock_guard<std::mutex> lock(m_tasksMutex);
	m_idleHandler = std::move(handler);
}
