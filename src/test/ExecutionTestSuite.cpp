#include "Catch2.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "Condition.h"
#include "MessageCause.h"
#include "SerialQueue.h"
#include "StdexecPrelude.h"

#include <exec/task.hpp>

#include "Strand.h"
#include "WorkPools.h"

namespace
{
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

// Waits on `condition` until `flag` is set; counts how often it looked.
exec::task<void> waitFor(execution::Condition& condition, const std::atomic<bool>& flag, std::atomic<int>& looks)
{
	co_await condition.until([&]() {
		looks++;
		return flag.load();
	});
}

// Sets `flag` once `delay` passed, and tells `condition`.
exec::task<void> setAfter(execution::Condition& condition, std::atomic<bool>& flag, std::chrono::milliseconds delay)
{
	co_await execution::delay(delay);
	flag = true;
	condition.notify();
}

exec::task<void> fail()
{
	co_await execution::delay(std::chrono::milliseconds(5));
	throw std::runtime_error("a stream failed");
}

auto onStrandOfItsOwn(exec::task<void> stream)
{
	return stdexec::starts_on(execution::Strand::create(execution::workPool())->scheduler(), std::move(stream));
}
}	 // namespace

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

TEST_CASE("a strand with a cause runs its work on behalf of it")
{
	const auto strand = execution::Strand::create(execution::fallbackPool(), 42);
	std::atomic<std::uint64_t> seen = 0;
	strand->post([&]() { seen = currentCause(); });
	REQUIRE(eventually([&]() { return seen.load() == 42; }));

	// Without one, an item keeps the cause it brings along.
	const auto plain = execution::Strand::create(execution::fallbackPool());
	std::atomic<std::uint64_t> carried = 1;
	{
		const CauseScope scope(7);
		plain->post(withCurrentCause([&]() { carried = currentCause(); }));
	}
	REQUIRE(eventually([&]() { return carried.load() == 7; }));
}

TEST_CASE("a condition's wait completes at once when what it waits for holds")
{
	execution::Condition condition;
	REQUIRE(stdexec::sync_wait(condition.until([]() { return true; })).has_value());
}

TEST_CASE("a condition's wait completes once what it waits for holds, not before")
{
	execution::Condition condition;
	std::atomic<bool> flag = false;
	std::atomic<int> looks = 0;
	std::atomic<bool> ended = false;
	exec::async_scope scope;
	scope.spawn(
		onStrandOfItsOwn(waitFor(condition, flag, looks)) |
		stdexec::then([&]() { ended = true; }));
	REQUIRE(eventually([&]() { return looks.load() == 1; }));

	condition.notify();	// what it waits for has not changed
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	REQUIRE(looks == 2);	// looked once more, not polled
	REQUIRE_FALSE(ended);

	flag = true;
	condition.notify();
	REQUIRE(eventually([&]() { return ended.load(); }));
	stdexec::sync_wait(scope.on_empty());
}

TEST_CASE("a condition's notify right as the wait starts is not missed")
{
	execution::Condition condition;
	for (int i = 0; i < 300; i++)
	{
		std::atomic<bool> flag = false;
		std::thread notifier([&]() {
			flag = true;
			condition.notify();
		});
		REQUIRE(stdexec::sync_wait(condition.until([&]() { return flag.load(); })).has_value());
		notifier.join();
	}
}

TEST_CASE("a stopped condition wait completes stopped")
{
	execution::Condition condition;
	std::atomic<bool> flag = false;
	std::atomic<int> looks = 0;
	std::atomic<int> outcome = 0;
	exec::async_scope scope;
	scope.spawn(
		onStrandOfItsOwn(waitFor(condition, flag, looks)) |
		stdexec::then([&]() { outcome = 1; }) | stdexec::upon_stopped([&]() { outcome = 2; }));
	REQUIRE(eventually([&]() { return looks.load() == 1; }));

	scope.request_stop();
	REQUIRE(eventually([&]() { return outcome.load() == 2; }));
	stdexec::sync_wait(scope.on_empty());

	flag = true;
	condition.notify();	// nobody waits any more
	REQUIRE(looks == 1);
}

TEST_CASE("coroutine streams side by side hold no thread while they wait")
{
	// One pool thread for both: the waiting stream must not hold it, or the one it
	// waits for could never run.
	const TestWorkPool work(1);
	execution::Condition condition;
	std::atomic<bool> flag = false;
	std::atomic<int> looks = 0;
	const auto result = stdexec::sync_wait(stdexec::when_all(
		onStrandOfItsOwn(waitFor(condition, flag, looks)),
		onStrandOfItsOwn(setAfter(condition, flag, std::chrono::milliseconds(20)))));
	REQUIRE(result.has_value());
	REQUIRE(flag);
}

TEST_CASE("a coroutine stream that throws stops the others")
{
	const TestWorkPool work(2);
	execution::Condition condition;
	const std::atomic<bool> never = false;
	std::atomic<int> looks = 0;
	REQUIRE_THROWS_AS(
		stdexec::sync_wait(stdexec::when_all(
			onStrandOfItsOwn(waitFor(condition, never, looks)), onStrandOfItsOwn(fail()))),
		std::runtime_error);
	REQUIRE(looks == 1);
}
