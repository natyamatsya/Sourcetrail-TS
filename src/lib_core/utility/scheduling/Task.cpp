#include "Task.h"

Task::TaskState Task::update(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	if (!m_enterCalled)
	{
		doEnter(blackboard);
		m_enterCalled = true;
	}

	const TaskState state = doUpdate(blackboard);

	if ((state == STATE_SUCCESS || state == STATE_FAILURE) && !m_exitCalled)
	{
		doExit(blackboard);
		m_exitCalled = true;
	}

	return state;
}

void Task::reset(std::shared_ptr<Blackboard> blackboard)
{
	doReset(blackboard);
	m_enterCalled = false;
	m_exitCalled = false;
}

void Task::terminate() {}
