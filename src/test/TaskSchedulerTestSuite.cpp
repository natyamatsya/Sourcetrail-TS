#include "Catch2.hpp"

#include <chrono>
#include <thread>

#include "Blackboard.h"
#include "Task.h"
#include "TaskFinally.h"
#include "TaskGroupSelector.h"
#include "TaskGroupSequence.h"
#include "TaskScheduler.h"

using enum Task::TaskState;

namespace
{
void executeTask(Task& task)
{
	std::shared_ptr<Blackboard> blackboard = std::make_shared<Blackboard>();
	while (true)
	{
		if (task.update(blackboard) != STATE_RUNNING)
		{
			return;
		}
	}
}

class TestTask: public Task
{
public:
	TestTask(int* orderCountPtr, int updateCount, TaskState returnState = STATE_SUCCESS)
		: orderCount(*orderCountPtr)
		, updateCount(updateCount)
		, returnState(returnState)
	{
	}

	void doEnter(std::shared_ptr<Blackboard>  /*blackboard*/) override
	{
		enterCallOrder = ++orderCount;
	}

	TaskState doUpdate(std::shared_ptr<Blackboard>  /*blackboard*/) override
	{
		updateCallOrder = ++orderCount;

		if (updateCount < 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			return STATE_RUNNING;
		}

		updateCount--;
		if (updateCount)
		{
			return STATE_RUNNING;
		}

		return returnState;
	}

	void doExit(std::shared_ptr<Blackboard>  /*blackboard*/) override
	{
		exitCallOrder = ++orderCount;
	}

	void doReset(std::shared_ptr<Blackboard>  /*blackboard*/) override
	{
		resetCallOrder = ++orderCount;
	}

	int& orderCount;
	int updateCount;
	TaskState returnState;

	int enterCallOrder = 0;
	int updateCallOrder = 0;
	int exitCallOrder = 0;
	int resetCallOrder = 0;
};

class TestTaskDispatch: public TestTask
{
public:
	TestTaskDispatch(int* orderCountPtr, int updateCount, TaskScheduler* scheduler)
		: TestTask(orderCountPtr, updateCount), scheduler(scheduler)
	{
	}

	TaskState doUpdate(std::shared_ptr<Blackboard> blackboard) override
	{
		subTask = std::make_shared<TestTask>(&orderCount, 1);
		scheduler->pushTask(subTask);

		return TestTask::doUpdate(blackboard);
	}

	TaskScheduler* scheduler;
	std::shared_ptr<TestTask> subTask;
};

void waitForThread(TaskScheduler& scheduler)
{
	static const int THREAD_WAIT_TIME_MS = 20;
	do
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_WAIT_TIME_MS));
	} while (scheduler.hasTasksQueued());
}
}	 // namespace

TEST_CASE("scheduler loop starts and stops")
{
	TaskScheduler scheduler(TabId::NONE);
	REQUIRE(!scheduler.loopIsRunning());

	scheduler.startSchedulerLoopThreaded();

	waitForThread(scheduler);

	REQUIRE(scheduler.loopIsRunning());

	scheduler.stopSchedulerLoop();

	waitForThread(scheduler);

	REQUIRE(!scheduler.loopIsRunning());
}

TEST_CASE("tasks get executed without scheduling in correct order")
{
	int order = 0;
	TestTask task(&order, 1);

	executeTask(task);

	REQUIRE(3 == order);

	REQUIRE(1 == task.enterCallOrder);
	REQUIRE(2 == task.updateCallOrder);
	REQUIRE(3 == task.exitCallOrder);
}

TEST_CASE("scheduled tasks get processed with callbacks in correct order")
{
	TaskScheduler scheduler(TabId::NONE);
	scheduler.startSchedulerLoopThreaded();

	int order = 0;
	std::shared_ptr<TestTask> task = std::make_shared<TestTask>(&order, 1);

	scheduler.pushTask(task);

	waitForThread(scheduler);

	scheduler.stopSchedulerLoop();

	REQUIRE(3 == order);

	REQUIRE(1 == task->enterCallOrder);
	REQUIRE(2 == task->updateCallOrder);
	REQUIRE(3 == task->exitCallOrder);
}

TEST_CASE("sequential task group to process tasks in correct order")
{
	TaskScheduler scheduler(TabId::NONE);
	scheduler.startSchedulerLoopThreaded();

	int order = 0;
	std::shared_ptr<TestTask> task1 = std::make_shared<TestTask>(&order, 1);
	std::shared_ptr<TestTask> task2 = std::make_shared<TestTask>(&order, 1);

	std::shared_ptr<TaskGroupSequence> taskGroup = std::make_shared<TaskGroupSequence>();
	taskGroup->addTask(task1);
	taskGroup->addTask(task2);

	scheduler.pushTask(taskGroup);

	waitForThread(scheduler);

	scheduler.stopSchedulerLoop();

	REQUIRE(6 == order);

	REQUIRE(1 == task1->enterCallOrder);
	REQUIRE(2 == task1->updateCallOrder);
	REQUIRE(3 == task1->exitCallOrder);

	REQUIRE(4 == task2->enterCallOrder);
	REQUIRE(5 == task2->updateCallOrder);
	REQUIRE(6 == task2->exitCallOrder);
}

TEST_CASE("sequential task group does not evaluate tasks after failure")
{
	TaskScheduler scheduler(TabId::NONE);
	scheduler.startSchedulerLoopThreaded();

	int order = 0;
	std::shared_ptr<TestTask> task1 = std::make_shared<TestTask>(&order, 1, STATE_FAILURE);
	std::shared_ptr<TestTask> task2 = std::make_shared<TestTask>(&order, -1);

	std::shared_ptr<TaskGroupSequence> taskGroup = std::make_shared<TaskGroupSequence>();
	taskGroup->addTask(task1);
	taskGroup->addTask(task2);

	scheduler.pushTask(taskGroup);

	waitForThread(scheduler);

	scheduler.stopSchedulerLoop();

	REQUIRE(1 == task1->enterCallOrder);
	REQUIRE(2 == task1->updateCallOrder);
	REQUIRE(3 == task1->exitCallOrder);

	REQUIRE(0 == task2->enterCallOrder);
	REQUIRE(0 == task2->updateCallOrder);
	REQUIRE(0 == task2->exitCallOrder);
}

TEST_CASE("sequential task group does not evaluate tasks after success")
{
	TaskScheduler scheduler(TabId::NONE);
	scheduler.startSchedulerLoopThreaded();

	int order = 0;
	std::shared_ptr<TestTask> task1 = std::make_shared<TestTask>(&order, 1, STATE_FAILURE);
	std::shared_ptr<TestTask> task2 = std::make_shared<TestTask>(&order, 1, STATE_SUCCESS);
	std::shared_ptr<TestTask> task3 = std::make_shared<TestTask>(&order, -1);

	std::shared_ptr<TaskGroupSelector> taskGroup = std::make_shared<TaskGroupSelector>();
	taskGroup->addTask(task1);
	taskGroup->addTask(task2);
	taskGroup->addTask(task3);

	scheduler.pushTask(taskGroup);

	waitForThread(scheduler);

	scheduler.stopSchedulerLoop();

	REQUIRE(1 == task1->enterCallOrder);
	REQUIRE(2 == task1->updateCallOrder);
	REQUIRE(3 == task1->exitCallOrder);

	REQUIRE(4 == task2->enterCallOrder);
	REQUIRE(5 == task2->updateCallOrder);
	REQUIRE(6 == task2->exitCallOrder);

	REQUIRE(0 == task3->enterCallOrder);
	REQUIRE(0 == task3->updateCallOrder);
	REQUIRE(0 == task3->exitCallOrder);
}

TEST_CASE("task scheduling within task processing")
{
	TaskScheduler scheduler(TabId::NONE);
	scheduler.startSchedulerLoopThreaded();

	int order = 0;
	std::shared_ptr<TestTaskDispatch> task = std::make_shared<TestTaskDispatch>(
		&order, 1, &scheduler);

	scheduler.pushTask(task);

	waitForThread(scheduler);

	scheduler.stopSchedulerLoop();

	REQUIRE(6 == order);

	REQUIRE(1 == task->enterCallOrder);
	REQUIRE(2 == task->updateCallOrder);
	REQUIRE(3 == task->exitCallOrder);

	REQUIRE(4 == task->subTask->enterCallOrder);
	REQUIRE(5 == task->subTask->updateCallOrder);
	REQUIRE(6 == task->subTask->exitCallOrder);
}

namespace
{
// A task whose update throws -- exercising the TaskRunner exception boundary that TaskFinally
// relies on to convert throws into STATE_FAILURE.
class ThrowingTask: public Task
{
public:
	void doEnter(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
	TaskState doUpdate(std::shared_ptr<Blackboard>  /*blackboard*/) override
	{
		throw std::runtime_error("task deliberately failed");
	}
	void doExit(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
	void doReset(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
};

struct TerminalRecord
{
	int calls = 0;
	TaskFinally::TerminalCause cause = TaskFinally::TerminalCause::Success;
};

std::shared_ptr<TaskFinally> makeFinally(TerminalRecord& record, std::shared_ptr<Task> child)
{
	auto finally = std::make_shared<TaskFinally>([&record](TaskFinally::TerminalCause cause) {
		record.calls++;
		record.cause = cause;
	});
	finally->addChildTask(child);
	return finally;
}
}	 // namespace

TEST_CASE("task finally signals success once")
{
	int order = 0;
	TerminalRecord record;
	std::shared_ptr<TaskFinally> task = makeFinally(record, std::make_shared<TestTask>(&order, 1));

	executeTask(*task);

	REQUIRE(1 == record.calls);
	REQUIRE(TaskFinally::TerminalCause::Success == record.cause);
}

TEST_CASE("task finally signals failure once")
{
	int order = 0;
	TerminalRecord record;
	std::shared_ptr<TaskFinally> task =
		makeFinally(record, std::make_shared<TestTask>(&order, 1, STATE_FAILURE));

	executeTask(*task);

	REQUIRE(1 == record.calls);
	REQUIRE(TaskFinally::TerminalCause::Failure == record.cause);
}

TEST_CASE("task finally converts a throwing stage into a failure signal")
{
	TerminalRecord record;
	std::shared_ptr<TaskFinally> task = makeFinally(record, std::make_shared<ThrowingTask>());

	executeTask(*task);

	REQUIRE(1 == record.calls);
	REQUIRE(TaskFinally::TerminalCause::Failure == record.cause);
}

TEST_CASE("task finally signals termination and stays fired-once")
{
	int order = 0;
	TerminalRecord record;
	std::shared_ptr<TaskFinally> task =
		makeFinally(record, std::make_shared<TestTask>(&order, -1));

	task->terminate();

	REQUIRE(1 == record.calls);
	REQUIRE(TaskFinally::TerminalCause::Terminated == record.cause);

	// A later terminal state must not re-fire the callback.
	task->terminate();
	REQUIRE(1 == record.calls);
}
