#ifndef TASK_GROUP_PARALLEL_H
#define TASK_GROUP_PARALLEL_H

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#include "TaskGroup.h"
#include "TaskRunner.h"

class TaskGroupParallel: public TaskGroup
{
public:
	TaskGroupParallel();

	void addTask(std::shared_ptr<Task> task) override;

private:
	struct TaskInfo
	{
		TaskInfo(std::shared_ptr<TaskRunner> taskRunner): taskRunner(taskRunner), active(false) {}
		std::shared_ptr<TaskRunner> taskRunner;
		std::shared_ptr<std::thread> thread;
		std::atomic<bool> active;
	};

	void doEnter(std::shared_ptr<Blackboard> blackboard) override;
	TaskState doUpdate(std::shared_ptr<Blackboard> blackboard) override;
	void doExit(std::shared_ptr<Blackboard> blackboard) override;
	void doReset(std::shared_ptr<Blackboard> blackboard) override;
	void doTerminate() override;

	void processTaskThreaded(
		std::shared_ptr<TaskInfo> taskInfo,
		std::shared_ptr<Blackboard> blackboard);
	// Wakes the parked group (doUpdate), if it is.
	void wake();

	std::vector<std::shared_ptr<TaskInfo>> m_tasks;
	bool m_needsToStartThreads = true;

	std::atomic<bool> m_taskFailed;
	std::atomic<int> m_activeTaskCount;
	std::mutex m_wakeMutex;
	std::function<void()> m_wake;	// set while the group is parked
};

#endif	  // TASK_GROUP_PARALLEL_H
