#ifndef TASK_GROUP_PARALLEL_H
#define TASK_GROUP_PARALLEL_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "TaskGroup.h"
#include "TaskRunner.h"
#include "TaskWake.h"

namespace execution
{
class Strand;
}

// Runs its children side by side and ends once all of them ended: failed if one
// failed. Each child runs on a strand of its own over the work pool
// (execution::workPool), in turns: a child updated until it waits holds a pool
// thread meanwhile, a child that waits parked (TaskScheduler::parkCurrentTask,
// restFor) holds none until it is woken. The group itself waits parked until the
// last child ended.
class TaskGroupParallel: public TaskGroup
{
public:
	TaskGroupParallel();

	void addTask(std::shared_ptr<Task> task) override;

private:
	struct Child;
	// What the children's turns share with the group: they may outlive it by a turn.
	struct Shared
	{
		std::vector<std::shared_ptr<Child>> children;
		std::atomic<bool> failed = false;
		std::mutex mutex;
		int running = 0;	// children that have not ended
		std::condition_variable allEnded;	// for doTerminate
		TaskWake childEnded;	// the group waits for it parked
	};

	void doEnter(std::shared_ptr<Blackboard> blackboard) override;
	TaskState doUpdate(std::shared_ptr<Blackboard> blackboard) override;
	void doExit(std::shared_ptr<Blackboard> blackboard) override;
	void doReset(std::shared_ptr<Blackboard> blackboard) override;
	void doTerminate() override;

	// Starts `child`'s turns.
	static void start(const std::shared_ptr<Shared>& shared, const std::shared_ptr<Child>& child);
	// On the child's strand: updates it until it waits or ends, a few updates at a time.
	static void turn(const std::shared_ptr<Shared>& shared, const std::shared_ptr<Child>& child);

	const std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
	bool m_started = false;
};

#endif	  // TASK_GROUP_PARALLEL_H
