#ifndef TASK_FINISH_PARSING_H
#define TASK_FINISH_PARSING_H

#include <atomic>
#include <memory>
#include <vector>

#include "Task.h"
#include "TaskWake.h"

class DialogView;
class FileRegister;
class PersistentStorage;
class StorageAccess;

class TaskFinishParsing: public Task
{
public:
	TaskFinishParsing(std::shared_ptr<PersistentStorage> storage, std::shared_ptr<DialogView> dialogView);

	void terminate() override;

private:
	void doEnter(std::shared_ptr<Blackboard> blackboard) override;
	TaskState doUpdate(std::shared_ptr<Blackboard> blackboard) override;
	void doExit(std::shared_ptr<Blackboard> blackboard) override;
	void doReset(std::shared_ptr<Blackboard> blackboard) override;

	std::shared_ptr<PersistentStorage> m_storage;
	std::shared_ptr<DialogView> m_dialogView;
	// The decision on the report once it is shown: -1 until someone decided, when
	// the task (waiting meanwhile) is woken.
	struct Decision
	{
		std::atomic<int> policy = -1;
		TaskWake decided;
	};
	std::shared_ptr<Decision> m_decision;
};

#endif	  // TASK_FINISH_PARSING_H
