#ifndef TASK_FINISH_PARSING_H
#define TASK_FINISH_PARSING_H

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "Task.h"

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
	// The decision on the report once it is shown, -1 until someone decided, and how
	// to wake the task then (parked meanwhile).
	struct Decision
	{
		std::mutex mutex;
		int policy = -1;
		std::function<void()> wake;
	};
	std::shared_ptr<Decision> m_decision;
};

#endif	  // TASK_FINISH_PARSING_H
