#include "TaskGroupParallel.h"

#include <thread>

#include "ScopedFunctor.h"
#include "TaskScheduler.h"

TaskGroupParallel::TaskGroupParallel() = default;

void TaskGroupParallel::addTask(std::shared_ptr<Task> task)
{
	m_tasks.push_back(std::make_shared<TaskInfo>(std::make_shared<TaskRunner>(task)));
}

void TaskGroupParallel::doEnter(std::shared_ptr<Blackboard> blackboard)
{
	m_taskFailed = false;

	if (m_needsToStartThreads)
	{
		m_needsToStartThreads = false;
		m_activeTaskCount = static_cast<int>(m_tasks.size());
		for (size_t i = 0; i < m_tasks.size(); i++)
		{
			m_tasks[i]->active = true;
			m_tasks[i]->thread = std::make_shared<std::thread>(
				&TaskGroupParallel::processTaskThreaded,
				this,
				m_tasks[i],
				blackboard);
		}
	}
}

Task::TaskState TaskGroupParallel::doUpdate(std::shared_ptr<Blackboard>  /*blackboard*/)
{
	using enum Task::TaskState;
	if (m_tasks.size() != 0 && m_activeTaskCount > 0)
	{
		// Parked until a child ends, holding no thread of its scheduler.
		if (std::function<void()> wake = TaskScheduler::parkCurrentTask())
		{
			{
				const std::lock_guard<std::mutex> lock(m_wakeMutex);
				m_wake = std::move(wake);
			}
			if (m_activeTaskCount == 0)
			{
				this->wake();	// the last child ended meanwhile
			}
			return STATE_RUNNING;
		}
		TaskScheduler::waitForNextLook();
		if (m_activeTaskCount > 0)
		{
			return STATE_RUNNING;
		}
	}

	return (m_taskFailed ? STATE_FAILURE : STATE_SUCCESS);
}

void TaskGroupParallel::wake()
{
	std::function<void()> wake;
	{
		const std::lock_guard<std::mutex> lock(m_wakeMutex);
		wake = std::exchange(m_wake, nullptr);
	}
	if (wake)
	{
		wake();
	}
}

void TaskGroupParallel::doExit(std::shared_ptr<Blackboard>  /*blackboard*/)
{
	for (size_t i = 0; i < m_tasks.size(); i++)
	{
		m_tasks[i]->thread->join();
		m_tasks[i]->thread.reset();
	}
}

void TaskGroupParallel::doReset(std::shared_ptr<Blackboard> blackboard)
{
	for (size_t i = 0; i < m_tasks.size(); i++)
	{
		m_tasks[i]->taskRunner->reset();
		if (!m_tasks[i]->active)
		{
			m_activeTaskCount++;

			m_tasks[i]->thread->join();
			m_tasks[i]->active = true;
			m_tasks[i]->thread = std::make_shared<std::thread>(
				&TaskGroupParallel::processTaskThreaded,
				this,
				m_tasks[i],
				blackboard);
		}
	}
}

void TaskGroupParallel::doTerminate()
{
	for (size_t i = 0; i < m_tasks.size(); i++)
	{
		m_tasks[i]->taskRunner->terminate();
	}

	for (size_t i = 0; i < m_tasks.size(); i++)
	{
		if (m_tasks[i]->thread)
		{
			m_tasks[i]->thread->join();
			m_tasks[i]->thread.reset();
		}
	}
}

void TaskGroupParallel::processTaskThreaded(std::shared_ptr<TaskInfo> taskInfo,
	std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	[[maybe_unused]]
	ScopedFunctor functor([&]()
	{
		m_activeTaskCount--;
		wake();
	});

	while (true)
	{
		TaskState state = taskInfo->taskRunner->update(blackboard);

		if (state == STATE_SUCCESS || state == STATE_FAILURE)
		{
			if (state == STATE_FAILURE)
			{
				m_taskFailed = true;
			}
			taskInfo->active = false;
			break;
		}
	}
}
