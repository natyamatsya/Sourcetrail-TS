#include "TaskRunner.h"

#include "Blackboard.h"
#include "TaskManager.h"
#include "TaskScheduler.h"
#include "logging.h"

TaskRunner::TaskRunner(std::shared_ptr<Task> task): m_task(task) {}

Task::TaskState TaskRunner::update(TabId schedulerId)
{
	if (!m_blackboard)
	{
		m_blackboard = std::make_shared<Blackboard>();
		m_blackboard->set<TabId>("scheduler_id", schedulerId);
	}

	return update(m_blackboard);
}

Task::TaskState TaskRunner::update(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	if (!blackboard)
	{
		if (!m_blackboard)
		{
			m_blackboard = std::make_shared<Blackboard>();
		}

		blackboard = m_blackboard;
	}

	try
	{
		if (m_reset)
		{
			m_task->reset(blackboard);
			m_reset = false;
		}

		return m_task->update(blackboard);
	}
	catch (std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	catch (...)
	{
		LOG_ERROR("Unknown exception thrown during task running");
	}

	TabId schedulerId = TabId::NONE;
	if (blackboard->get<TabId>("scheduler_id", schedulerId))
	{
		TaskManager::getScheduler(schedulerId)->terminateRunningTasks();
	}

	return STATE_FAILURE;
}

void TaskRunner::reset()
{
	m_reset = true;
}

void TaskRunner::terminate()
{
	if (m_task)
	{
		m_task->terminate();
	}
}
