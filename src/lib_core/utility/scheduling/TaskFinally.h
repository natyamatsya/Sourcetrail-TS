#ifndef TASK_FINALLY_H
#define TASK_FINALLY_H

#include <functional>

#include "TaskDecorator.h"

// Guarantees a terminal notification for a task tree: whatever way the child ends -- success,
// failure, an exception (the child's TaskRunner converts throws into STATE_FAILURE), or
// termination -- the callback fires exactly once with the cause. Wrap a pipeline's ROOT in this
// so the terminal event is owned by the pipeline, not by some stage inside it that an earlier
// failure can skip (the bug class: a headless run waiting forever on a completion message that
// a dead stage never sent).
class TaskFinally: public TaskDecorator
{
public:
	enum class TerminalCause
	{
		Success,
		Failure,	 // the child ended with STATE_FAILURE (including converted exceptions)
		Terminated,	 // terminate() ended the child before completion
	};

	using Callback = std::function<void(TerminalCause)>;

	explicit TaskFinally(Callback onTerminal);

private:
	void doEnter(std::shared_ptr<Blackboard> blackboard) override;
	TaskState doUpdate(std::shared_ptr<Blackboard> blackboard) override;
	void doExit(std::shared_ptr<Blackboard> blackboard) override;
	void doReset(std::shared_ptr<Blackboard> blackboard) override;
	void doTerminate() override;

	void signal(TerminalCause cause);

	Callback m_onTerminal;
	bool m_signalled = false;
};

#endif	  // TASK_FINALLY_H
