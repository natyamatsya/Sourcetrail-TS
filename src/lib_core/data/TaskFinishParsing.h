#ifndef TASK_FINISH_PARSING_H
#define TASK_FINISH_PARSING_H

#include <atomic>
#include <memory>

#include "Condition.h"
#include "IndexingStateMachine.h"
#include "StdexecPrelude.h"

#include <exec/task.hpp>

class DialogView;
class PersistentStorage;
struct IndexingRunState;

// The run's last step: readies the database for reading, reports the run, and waits
// (holding no thread) for the decision on its data.
class TaskFinishParsing
{
public:
	TaskFinishParsing(std::shared_ptr<PersistentStorage> storage, std::shared_ptr<DialogView> dialogView);

	//! The decision on the run's data.
	exec::task<DatabasePolicy> run(const IndexingRunState& state);

private:
	void report(const IndexingRunState& state);

	std::shared_ptr<PersistentStorage> m_storage;
	std::shared_ptr<DialogView> m_dialogView;
	// The decision on the report once it is shown: -1 until someone decided. Shared
	// with the report's callback, which may come after the step is gone.
	struct Decision
	{
		std::atomic<int> policy = -1;
		execution::Condition decided;
	};
	std::shared_ptr<Decision> m_decision;
};

#endif	  // TASK_FINISH_PARSING_H
