#include "TaskScheduler.h"

#include <chrono>
#include <thread>

#include "ScopedFunctor.h"
#include "logging.h"

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
	std::lock_guard<std::mutex> lock(m_tasksMutex);
	m_taskRunners.push_back(std::make_shared<TaskRunner>(task));
}

void TaskScheduler::pushNextTask(std::shared_ptr<Task> task)
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
	std::lock_guard<std::mutex> lock(m_tasksMutex);

	while (m_taskRunners.size())
	{
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
}
