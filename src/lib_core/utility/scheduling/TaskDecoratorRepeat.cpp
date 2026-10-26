#include "TaskDecoratorRepeat.h"

#include <chrono>

#include "TaskScheduler.h"

TaskDecoratorRepeat::TaskDecoratorRepeat(ConditionType condition, TaskState exitState, size_t delayMS)
	: m_condition(condition), m_exitState(exitState), m_delayMS(delayMS)
{
}

void TaskDecoratorRepeat::doEnter(std::shared_ptr<Blackboard>  /*blackboard*/) {}

Task::TaskState TaskDecoratorRepeat::doUpdate(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	using enum TaskDecoratorRepeat::ConditionType;
	TaskState state = m_taskRunner->update(blackboard);

	switch (m_condition)
	{
	case CONDITION_WHILE_SUCCESS:
		if (state == STATE_SUCCESS)
		{
			m_taskRunner->reset();
			state = STATE_RUNNING;
		}
		else if (state == STATE_FAILURE)
		{
			return m_exitState;
		}
		break;
	}

	// Looks again after the delay, parked meanwhile (TaskScheduler::restFor).
	if (state == STATE_RUNNING && m_delayMS > 0)
	{
		TaskScheduler::restFor(std::chrono::milliseconds(m_delayMS));
	}

	return state;
}

void TaskDecoratorRepeat::doExit(std::shared_ptr<Blackboard>  /*blackboard*/) {}

void TaskDecoratorRepeat::doReset(std::shared_ptr<Blackboard>  /*blackboard*/)
{
	m_taskRunner->reset();
}
