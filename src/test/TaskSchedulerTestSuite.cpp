#include "Catch2.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <functional>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>

#include "Blackboard.h"
#include "Task.h"
#include "TaskFinally.h"
#include "TaskGroupSelector.h"
#include "TaskGroupParallel.h"
#include "TaskGroupSequence.h"
#include "MessageCause.h"
#include "SerialQueue.h"
#include "StdexecPrelude.h"
#include "Strand.h"
#include "TaskLambda.h"
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

// Holds until released, as a background task waiting on its work does.
class HoldingTask: public Task
{
public:
	std::atomic<bool> released = false;
	std::atomic<int> updates = 0;

private:
	void doEnter(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
	TaskState doUpdate(std::shared_ptr<Blackboard>  /*blackboard*/) override
	{
		updates++;
		return released ? STATE_SUCCESS : STATE_HOLD;
	}
	void doExit(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
	void doReset(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
};

template <typename Predicate>
bool eventually(Predicate predicate)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (predicate())
		{
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return predicate();
}

// Parks itself until woken, then succeeds, as a task waiting for an answer does.
class ParkingTask: public Task
{
public:
	std::atomic<int> updates = 0;
	std::atomic<bool> terminated = false;
	std::mutex mutex;
	std::function<void()> wake;
	bool woken = false;

	void terminate() override
	{
		terminated = true;
	}

	void wakeUp()
	{
		std::function<void()> wakeNow;
		{
			const std::lock_guard<std::mutex> lock(mutex);
			woken = true;
			wakeNow = std::exchange(wake, nullptr);
		}
		if (wakeNow)
		{
			wakeNow();
		}
	}

private:
	void doEnter(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
	TaskState doUpdate(std::shared_ptr<Blackboard>  /*blackboard*/) override
	{
		updates++;
		const std::lock_guard<std::mutex> lock(mutex);
		if (woken)
		{
			return STATE_SUCCESS;
		}
		wake = TaskScheduler::parkCurrentTask();
		return STATE_RUNNING;
	}
	void doExit(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
	void doReset(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
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

TEST_CASE("scheduler on a strand runs its tasks in order")
{
	exec::static_thread_pool pool(2);
	const auto strand = execution::Strand::create(pool.get_scheduler());
	const auto scheduler = std::make_shared<TaskScheduler>(TabId::NONE);

	std::vector<int> order;	// written on the strand only
	scheduler->pushTask(std::make_shared<TaskLambda>([&]() { order.push_back(1); }));	// before it starts
	scheduler->startSchedulerLoopOn(strand);
	scheduler->pushTask(std::make_shared<TaskLambda>([&]() { order.push_back(strand->runsHere() ? 2 : -2); }));
	scheduler->pushTask(std::make_shared<TaskLambda>([&]() { order.push_back(3); }));

	REQUIRE(eventually([&]() { return !scheduler->hasTasksQueued(); }));
	scheduler->stopSchedulerLoop();
	REQUIRE(order == std::vector<int> {1, 2, 3});
	REQUIRE_FALSE(scheduler->loopIsRunning());
}

TEST_CASE("scheduler on a strand lets other work run while a task holds")
{
	exec::static_thread_pool pool(2);
	const auto strand = execution::Strand::create(pool.get_scheduler());
	const auto scheduler = std::make_shared<TaskScheduler>(TabId::NONE);
	scheduler->startSchedulerLoopOn(strand);

	const auto holding = std::make_shared<HoldingTask>();
	scheduler->pushTask(holding);
	REQUIRE(eventually([&]() { return holding->updates > 1; }));

	// A pipeline continuing on the strand gets its turn between the task's passes.
	std::atomic<bool> continued = false;
	stdexec::sync_wait(
		stdexec::schedule(strand->scheduler()) | stdexec::then([&]() { continued = strand->runsHere(); }));
	REQUIRE(continued);
	REQUIRE(scheduler->hasTasksQueued());
	REQUIRE(scheduler->hasTasksQueued());

	holding->released = true;
	REQUIRE(eventually([&]() { return !scheduler->hasTasksQueued(); }));
	REQUIRE_FALSE(scheduler->hasTasksQueued());
	scheduler->stopSchedulerLoop();
}

TEST_CASE("scheduler on a strand terminates what is left when it stops")
{
	exec::static_thread_pool pool(2);
	const auto strand = execution::Strand::create(pool.get_scheduler());
	const auto scheduler = std::make_shared<TaskScheduler>(TabId::NONE);
	scheduler->startSchedulerLoopOn(strand);

	const auto holding = std::make_shared<HoldingTask>();
	scheduler->pushTask(holding);
	REQUIRE(eventually([&]() { return holding->updates > 0; }));
	scheduler->stopSchedulerLoop();	// waits for the pass that terminates it
	REQUIRE_FALSE(scheduler->hasTasksQueued());
	const int updates = holding->updates;
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	REQUIRE(holding->updates == updates);
}

TEST_CASE("task parked on a strand is left alone until woken")
{
	exec::static_thread_pool pool(2);
	const auto strand = execution::Strand::create(pool.get_scheduler());
	const auto scheduler = std::make_shared<TaskScheduler>(TabId::NONE);
	scheduler->startSchedulerLoopOn(strand);

	const auto parking = std::make_shared<ParkingTask>();
	parking->setIsBackgroundTask(true);	// held, as a background task waits
	scheduler->pushTask(parking);
	REQUIRE(eventually([&]() { return parking->updates == 1; }));
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	REQUIRE(parking->updates == 1);	// no pass for a parked task
	REQUIRE(scheduler->hasTasksQueued());
	REQUIRE(scheduler->hasTasksQueued());

	// Other tasks run meanwhile.
	std::atomic<bool> ran = false;
	scheduler->pushTask(std::make_shared<TaskLambda>([&]() { ran = true; }));
	REQUIRE(eventually([&]() { return ran.load(); }));
	REQUIRE(parking->updates == 1);
	REQUIRE(scheduler->hasTasksQueued());

	parking->wakeUp();
	REQUIRE(eventually([&]() { return !scheduler->hasTasksQueued(); }));
	REQUIRE(parking->updates == 2);
	REQUIRE_FALSE(scheduler->hasTasksQueued());
	scheduler->stopSchedulerLoop();
}

TEST_CASE("task parked on a thread loop is looked at each loop, not spun")
{
	TaskScheduler scheduler(TabId::NONE);
	scheduler.startSchedulerLoopThreaded();

	const auto parking = std::make_shared<ParkingTask>();
	scheduler.pushTask(parking);
	REQUIRE(eventually([&]() { return parking->updates > 0; }));
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	REQUIRE(parking->updates < 20);	// about one a loop (25 ms), not thousands

	parking->wakeUp();
	REQUIRE(eventually([&]() { return !scheduler.hasTasksQueued(); }));
	scheduler.stopSchedulerLoop();
}

TEST_CASE("parallel group on a strand is woken by its children, not polled")
{
	exec::static_thread_pool pool(2);
	const auto strand = execution::Strand::create(pool.get_scheduler());
	const auto scheduler = std::make_shared<TaskScheduler>(TabId::NONE);
	scheduler->startSchedulerLoopOn(strand);

	std::atomic<bool> release = false;
	const auto group = std::make_shared<TaskGroupParallel>();
	group->addTask(std::make_shared<TaskLambda>([&]() {
		while (!release)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}));
	group->addTask(std::make_shared<TaskLambda>([]() {}));
	scheduler->pushTask(group);

	// While the slow child runs, the group holds no thread of the strand.
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	std::atomic<bool> continued = false;
	stdexec::sync_wait(stdexec::schedule(strand->scheduler()) | stdexec::then([&]() { continued = true; }));
	REQUIRE(continued);
	REQUIRE(scheduler->hasTasksQueued());

	const auto released = std::chrono::steady_clock::now();
	release = true;
	REQUIRE(eventually([&]() { return !scheduler->hasTasksQueued(); }));
	REQUIRE(std::chrono::steady_clock::now() - released < std::chrono::seconds(1));
	scheduler->stopSchedulerLoop();
}

TEST_CASE("task parked while running keeps the tasks behind it waiting")
{
	// As a running task always did: a parallel group's listeners run, and the next
	// message for the same controllers waits for them.
	const auto check = [](TaskScheduler& scheduler) {
		const auto parking = std::make_shared<ParkingTask>();
		std::atomic<bool> ran = false;
		scheduler.pushTask(parking);
		scheduler.pushTask(std::make_shared<TaskLambda>([&]() { ran = true; }));
		REQUIRE(eventually([&]() { return parking->updates > 0; }));
		std::this_thread::sleep_for(std::chrono::milliseconds(60));
		REQUIRE_FALSE(ran);

		parking->wakeUp();
		REQUIRE(eventually([&]() { return ran.load(); }));
		REQUIRE(eventually([&]() { return !scheduler.hasTasksQueued(); }));
	};

	exec::static_thread_pool pool(2);
	const auto onStrand = std::make_shared<TaskScheduler>(TabId::NONE);
	onStrand->startSchedulerLoopOn(execution::Strand::create(pool.get_scheduler()));
	check(*onStrand);
	onStrand->stopSchedulerLoop();

	TaskScheduler onThread(TabId::NONE);
	onThread.startSchedulerLoopThreaded();
	check(onThread);
	onThread.stopSchedulerLoop();
}

TEST_CASE("a parked task is terminated when its scheduler on a strand stops")
{
	// An indexing run waiting for its indexers: stopping the app must end it (and with
	// it the indexer processes), though nothing posts a pass for a parked task.
	exec::static_thread_pool pool(2);
	const auto scheduler = std::make_shared<TaskScheduler>(TabId::NONE);
	scheduler->startSchedulerLoopOn(execution::Strand::create(pool.get_scheduler()));

	const auto parking = std::make_shared<ParkingTask>();
	parking->setIsBackgroundTask(true);
	scheduler->pushTask(parking);
	REQUIRE(eventually([&]() { return parking->updates > 0; }));

	scheduler->stopSchedulerLoop();
	REQUIRE(parking->terminated);
	REQUIRE_FALSE(scheduler->hasTasksQueued());
}

namespace
{
// Parks once, and is woken by another thread as soon as it has: the wake can come
// right between its update and the scheduler looking at it again.
class WokenAtOnceTask: public Task
{
public:
	std::atomic<bool> completed = false;
	std::atomic<bool> parked = false;
	std::function<void()> wake;

private:
	void doEnter(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
	TaskState doUpdate(std::shared_ptr<Blackboard>  /*blackboard*/) override
	{
		if (parked)
		{
			completed = true;
			return STATE_SUCCESS;
		}
		wake = TaskScheduler::parkCurrentTask();
		parked = true;
		return STATE_RUNNING;
	}
	void doExit(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
	void doReset(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
};
}	 // namespace

TEST_CASE("a task woken right after it parked runs on, not dropped")
{
	const auto check = [](TaskScheduler& scheduler) {
		for (int i = 0; i < 300; i++)
		{
			const auto task = std::make_shared<WokenAtOnceTask>();
			scheduler.pushTask(task);
			while (!task->parked)
			{
				std::this_thread::yield();
			}
			task->wake();
			REQUIRE(eventually([&]() { return !scheduler.hasTasksQueued(); }));
			REQUIRE(task->completed);
		}
	};

	exec::static_thread_pool pool(2);
	const auto onStrand = std::make_shared<TaskScheduler>(TabId::NONE);
	onStrand->startSchedulerLoopOn(execution::Strand::create(pool.get_scheduler()));
	check(*onStrand);
	onStrand->stopSchedulerLoop();
}

TEST_CASE("serial queue runs its items in order, on its strand")
{
	exec::static_thread_pool pool(2);
	const auto strand = execution::Strand::create(pool.get_scheduler());
	const auto queue = execution::SerialQueue::create(strand);

	std::vector<int> order;	// written by the items only
	std::atomic<int> left = 40;
	for (int i = 0; i < 40; i++)	// more than a turn runs at once
	{
		queue->post([&, i]() {
			order.push_back(queue->runsHere() ? i : -1);
			left--;
		});
	}
	queue->post([&]() { order.push_back(-100); left--; }, true);	// may run before the rest
	queue->post([]() { throw std::runtime_error("an item that throws"); });	// logged; the rest go on
	queue->post([&]() { left--; });
	REQUIRE(eventually([&]() { return left == -2; }));
	std::vector<int> expected;
	for (int i = 0; i < 40; i++)
	{
		expected.push_back(i);
	}
	order.erase(std::remove(order.begin(), order.end(), -100), order.end());
	REQUIRE(order == expected);
}

TEST_CASE("serial queue holds an item that finishes later, not a thread")
{
	exec::static_thread_pool pool(2);
	exec::static_thread_pool elsewhere(1);
	const auto strand = execution::Strand::create(pool.get_scheduler());
	const auto queue = execution::SerialQueue::create(strand);

	std::atomic<bool> release = false;
	std::atomic<bool> nextRan = false;
	std::atomic<bool> nextRanEarly = false;
	queue->push({[&](std::function<void()> done) {
					 // Finishes on another pool, once released.
					 stdexec::start_detached(stdexec::schedule(elsewhere.get_scheduler()) | stdexec::then([&, done]() {
												 while (!release)
												 {
													 std::this_thread::yield();
												 }
												 done();
											 }));
				 },
				 {}});
	queue->post([&]() {
		nextRanEarly = !release;
		nextRan = true;
	});

	// The strand is free meanwhile: other work runs there.
	std::atomic<bool> strandFree = false;
	stdexec::sync_wait(stdexec::schedule(strand->scheduler()) | stdexec::then([&]() { strandFree = true; }));
	REQUIRE(strandFree);
	REQUIRE_FALSE(nextRan);

	release = true;
	REQUIRE(eventually([&]() { return nextRan.load(); }));
	REQUIRE_FALSE(nextRanEarly);

	// As a sender: what follows schedule() runs as an item.
	std::atomic<bool> onQueue = false;
	stdexec::sync_wait(stdexec::schedule(queue->scheduler()) | stdexec::then([&]() { onQueue = queue->runsHere(); }));
	REQUIRE(onQueue);
}

TEST_CASE("serial queue drops what is queued when it stops, and counts it as ended")
{
	exec::static_thread_pool pool(2);
	const auto queue = execution::SerialQueue::create(execution::Strand::create(pool.get_scheduler()));

	std::atomic<bool> release = false;
	std::atomic<bool> started = false;
	std::atomic<int> dropped = 0;
	{
		const CauseScope cause(31);
		queue->push({[&](std::function<void()> done) {
						 started = true;
						 std::thread([&release, done]() {
							 while (!release)
							 {
								 std::this_thread::yield();
							 }
							 done();
						 }).detach();
					 },
					 {}});
		queue->push({[](std::function<void()>) { FAIL("dropped work ran"); }, [&]() { dropped++; }});
		queue->post([]() { throw std::runtime_error("an item that throws"); });
	}
	REQUIRE(eventually([&]() { return started.load(); }));
	REQUIRE(CauseWork::pending(31) == 3);
	queue->stop();
	REQUIRE(dropped == 1);
	REQUIRE(CauseWork::pending(31) == 1);	// the one that runs
	REQUIRE_FALSE(queue->post([]() {}));	// nothing more
	release = true;
	REQUIRE(eventually([&]() { return CauseWork::pending(31) == 0; }));
}
