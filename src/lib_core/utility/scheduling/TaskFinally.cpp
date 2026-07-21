#include "TaskFinally.h"

#include <utility>

#include "TaskRunner.h"

TaskFinally::TaskFinally(Callback onTerminal): m_onTerminal(std::move(onTerminal)) {}

void TaskFinally::doEnter(std::shared_ptr<Blackboard>  /*blackboard*/) {}

Task::TaskState TaskFinally::doUpdate(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;

	const TaskState state = m_taskRunner->update(blackboard);
	if (state == STATE_SUCCESS)
	{
		signal(TerminalCause::Success);
	}
	else if (state == STATE_FAILURE)
	{
		signal(TerminalCause::Failure);
	}
	return state;
}

void TaskFinally::doExit(std::shared_ptr<Blackboard>  /*blackboard*/) {}

void TaskFinally::doReset(std::shared_ptr<Blackboard>  /*blackboard*/)
{
	m_taskRunner->reset();
	m_signalled = false;
}

void TaskFinally::doTerminate()
{
	// TaskDecorator::doTerminate is private-virtual, so forward to the runner directly.
	m_taskRunner->terminate();
	signal(TerminalCause::Terminated);
}

void TaskFinally::signal(TerminalCause cause)
{
	if (m_signalled || !m_onTerminal)
	{
		return;
	}
	m_signalled = true;
	m_onTerminal(cause);
}
