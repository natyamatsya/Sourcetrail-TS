#include "Catch2.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "Blackboard.h"
#include "MessageCause.h"
#include "SerialQueue.h"
#include "StdexecPrelude.h"

#include <exec/task.hpp>

#include "Strand.h"
#include "Task.h"
#include "TaskGroupSequence.h"
#include "TaskLambda.h"
#include "TaskParking.h"
#include "TaskSender.h"
#include "TaskWake.h"
#include "WorkPools.h"

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
		wake = TaskParking::parkCurrentTask();
		return STATE_RUNNING;
	}
	void doExit(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
	void doReset(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
};

// A task whose update throws: its runner turns the throw into STATE_FAILURE.
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

// Parks once, and is woken by another thread as soon as it has: the wake can come
// right between its update and its driver looking at it again.
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
		wake = TaskParking::parkCurrentTask();
		parked = true;
		return STATE_RUNNING;
	}
	void doExit(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
	void doReset(std::shared_ptr<Blackboard>  /*blackboard*/) override {}
};

// Runs `task` to its end as a sender, the way the indexing pipeline runs its steps.
Task::TaskState runToEnd(std::shared_ptr<Task> task)
{
	const auto result = stdexec::sync_wait(execution::runTask(std::move(task), std::make_shared<Blackboard>()));
	REQUIRE(result.has_value());
	return std::get<0>(*result);
}
}	 // namespace

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

TEST_CASE("a task sender runs its task with callbacks in correct order")
{
	int order = 0;
	std::shared_ptr<TestTask> task = std::make_shared<TestTask>(&order, 3);

	REQUIRE(runToEnd(task) == STATE_SUCCESS);

	REQUIRE(5 == order);
	REQUIRE(1 == task->enterCallOrder);
	REQUIRE(4 == task->updateCallOrder);
	REQUIRE(5 == task->exitCallOrder);
}

TEST_CASE("sequential task group to process tasks in correct order")
{
	int order = 0;
	std::shared_ptr<TestTask> task1 = std::make_shared<TestTask>(&order, 1);
	std::shared_ptr<TestTask> task2 = std::make_shared<TestTask>(&order, 1);

	std::shared_ptr<TaskGroupSequence> taskGroup = std::make_shared<TaskGroupSequence>();
	taskGroup->addTask(task1);
	taskGroup->addTask(task2);

	REQUIRE(runToEnd(taskGroup) == STATE_SUCCESS);

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
	int order = 0;
	std::shared_ptr<TestTask> task1 = std::make_shared<TestTask>(&order, 1, STATE_FAILURE);
	std::shared_ptr<TestTask> task2 = std::make_shared<TestTask>(&order, -1);

	std::shared_ptr<TaskGroupSequence> taskGroup = std::make_shared<TaskGroupSequence>();
	taskGroup->addTask(task1);
	taskGroup->addTask(task2);

	REQUIRE(runToEnd(taskGroup) == STATE_FAILURE);

	REQUIRE(1 == task1->enterCallOrder);
	REQUIRE(2 == task1->updateCallOrder);
	REQUIRE(3 == task1->exitCallOrder);

	REQUIRE(0 == task2->enterCallOrder);
	REQUIRE(0 == task2->updateCallOrder);
	REQUIRE(0 == task2->exitCallOrder);
}

TEST_CASE("a throwing task fails, and so does the sequence it is in")
{
	int order = 0;
	std::shared_ptr<TestTask> after = std::make_shared<TestTask>(&order, 1);
	std::shared_ptr<TaskGroupSequence> taskGroup = std::make_shared<TaskGroupSequence>();
	taskGroup->addTask(std::make_shared<ThrowingTask>());
	taskGroup->addTask(after);

	REQUIRE(runToEnd(std::make_shared<ThrowingTask>()) == STATE_FAILURE);
	REQUIRE(runToEnd(taskGroup) == STATE_FAILURE);
	REQUIRE(0 == after->enterCallOrder);
}

TEST_CASE("a parked task is left alone until woken")
{
	const auto parking = std::make_shared<ParkingTask>();
	exec::async_scope scope;
	std::atomic<bool> ended = false;
	scope.spawn(
		execution::runTask(parking, std::make_shared<Blackboard>()) |
		stdexec::then([&](Task::TaskState state) { ended = state == STATE_SUCCESS; }));
	REQUIRE(eventually([&]() { return parking->updates == 1; }));
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	REQUIRE(parking->updates == 1);	// no turn for a parked task
	REQUIRE_FALSE(ended);

	parking->wakeUp();
	REQUIRE(eventually([&]() { return ended.load(); }));
	REQUIRE(parking->updates == 2);
	stdexec::sync_wait(scope.on_empty());
}

TEST_CASE("a task woken right after it parked runs on, not dropped")
{
	exec::async_scope scope;
	for (int i = 0; i < 300; i++)
	{
		const auto task = std::make_shared<WokenAtOnceTask>();
		std::atomic<bool> ended = false;
		scope.spawn(
			execution::runTask(task, std::make_shared<Blackboard>()) | stdexec::then([&](Task::TaskState) { ended = true; }));
		while (!task->parked)
		{
			std::this_thread::yield();
		}
		task->wake();
		REQUIRE(eventually([&]() { return ended.load(); }));
		REQUIRE(task->completed);
	}
	stdexec::sync_wait(scope.on_empty());
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

namespace
{
// Waits, parked, until released (or terminated); then succeeds.
class WaitingTask: public Task
{
public:
	void release()
	{
		released = true;
		m_wake.notify();
	}
	void terminate() override
	{
		release();
	}

	std::atomic<bool> released = false;
	std::atomic<int> updates = 0;

private:
	void doEnter(std::shared_ptr<Blackboard>) override {}
	TaskState doUpdate(std::shared_ptr<Blackboard>) override
	{
		updates++;
		return m_wake.waitUntil([this]() { return released.load(); }) ? TaskState::STATE_SUCCESS
																		: TaskState::STATE_RUNNING;
	}
	void doExit(std::shared_ptr<Blackboard>) override {}
	void doReset(std::shared_ptr<Blackboard>) override {}

	TaskWake m_wake;
};

// Looks again a few times after a rest, then succeeds.
class RestingTask: public Task
{
public:
	explicit RestingTask(int rests): m_rests(rests) {}

	std::atomic<bool> done = false;

private:
	void doEnter(std::shared_ptr<Blackboard>) override {}
	TaskState doUpdate(std::shared_ptr<Blackboard>) override
	{
		if (m_rests-- > 0)
		{
			TaskParking::restFor(std::chrono::milliseconds(10));
			return TaskState::STATE_RUNNING;
		}
		done = true;
		return TaskState::STATE_SUCCESS;
	}
	void doExit(std::shared_ptr<Blackboard>) override {}
	void doReset(std::shared_ptr<Blackboard>) override {}

	int m_rests;
};

// The work pool for a test, and back to the fallback after it.
class TestWorkPool
{
public:
	explicit TestWorkPool(std::size_t threads): m_pool(threads)
	{
		execution::setWorkPool(execution::AnyScheduler(m_pool.get_scheduler()));
	}
	~TestWorkPool()
	{
		execution::setWorkPool(std::nullopt);
	}

private:
	exec::static_thread_pool m_pool;
};
}	 // namespace

TEST_CASE("parallelFor runs every part, also when no pool thread is free")
{
	const TestWorkPool work(1);
	// The one pool thread is busy: the caller runs the parts itself.
	std::atomic<bool> release = false;
	execution::runOnWorkPool([&]() {
		while (!release)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	});

	std::vector<std::atomic<int>> ran(50);
	execution::parallelFor(ran.size(), [&](std::size_t index) { ran[index]++; });
	for (const std::atomic<int>& count: ran)
	{
		REQUIRE(count == 1);
	}
	release = true;

	// What a part throws reaches the caller, once the rest ran.
	std::atomic<int> others = 0;
	REQUIRE_THROWS_AS(
		execution::parallelFor(
			8,
			[&](std::size_t index) {
				if (index == 3)
				{
					throw std::runtime_error("part 3");
				}
				others++;
			}),
		std::runtime_error);
	REQUIRE(others == 7);
}

namespace
{
// Two tasks one after another, with a rest between: what a pipeline's stages do.
exec::task<Task::TaskState> twoStages(std::shared_ptr<Blackboard> blackboard, std::atomic<int>& ran)
{
	const auto first = std::make_shared<TaskLambda>([&ran]() { ran++; });
	if (co_await execution::runTask(first, blackboard) == Task::TaskState::STATE_FAILURE)
	{
		co_return Task::TaskState::STATE_FAILURE;
	}
	co_await execution::delay(std::chrono::milliseconds(5));
	co_return co_await execution::runTask(std::make_shared<RestingTask>(3), blackboard);
}
}	 // namespace

TEST_CASE("a coroutine runs tasks one after another, holding no thread while they rest")
{
	const TestWorkPool work(1);
	std::atomic<int> ran = 0;
	const auto strand = execution::Strand::create(execution::workPool());
	const auto result = stdexec::sync_wait(
		stdexec::starts_on(strand->scheduler(), twoStages(std::make_shared<Blackboard>(), ran)));
	REQUIRE(result.has_value());
	REQUIRE(std::get<0>(*result) == Task::TaskState::STATE_SUCCESS);
	REQUIRE(ran == 1);
}

TEST_CASE("a stopped task sender terminates its task and completes stopped")
{
	const TestWorkPool work(1);
	const auto waiting = std::make_shared<WaitingTask>();
	exec::async_scope scope;
	std::atomic<int> outcome = 0;
	scope.spawn(
		execution::runTask(waiting, std::make_shared<Blackboard>()) |
		stdexec::then([&](Task::TaskState) { outcome = 1; }) | stdexec::upon_stopped([&]() { outcome = 2; }));
	REQUIRE(eventually([&]() { return waiting->updates > 0; }));

	scope.request_stop();
	REQUIRE(eventually([&]() { return outcome == 2; }));
	REQUIRE(waiting->released);	// terminated
	stdexec::sync_wait(scope.on_empty());
}
