#include "Catch2.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#include "SharedQueuePool.h"
#include "StdexecPrelude.h"

namespace
{
// A task step that holds its thread until released, as a strand's step that
// waits on a child process does.
class Hold
{
public:
	void wait()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_held = true;
		m_changed.notify_all();
		m_changed.wait(lock, [this]() { return m_released; });
	}

	void waitUntilHeld()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_changed.wait(lock, [this]() { return m_held; });
	}

	void release()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_released = true;
		m_changed.notify_all();
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_changed;
	bool m_held = false;
	bool m_released = false;
};

bool waitFor(const std::atomic<int>& counter, int value, std::chrono::milliseconds timeout)
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (counter.load() < value && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return counter.load() >= value;
}
}	 // namespace

TEST_CASE("shared queue pool runs work from outside it while one of its threads is held")
{
	// The case exec::static_thread_pool fails: work scheduled from a thread outside
	// the pool (thoth-ipc's reactor, the GUI thread) went to a thread picked at
	// random, and waited there for as long as that thread was held.
	execution::SharedQueuePool pool(4);
	Hold hold;
	stdexec::start_detached(stdexec::schedule(pool.scheduler()) | stdexec::then([&hold]() { hold.wait(); }));
	hold.waitUntilHeld();

	std::atomic<int> ran = 0;
	for (int i = 0; i < 40; i++)
	{
		stdexec::start_detached(stdexec::schedule(pool.scheduler()) | stdexec::then([&ran]() { ran++; }));
	}
	const bool allRan = waitFor(ran, 40, std::chrono::seconds(2));
	hold.release();

	REQUIRE(allRan);
}

TEST_CASE("shared queue pool runs as many steps side by side as it has threads")
{
	execution::SharedQueuePool pool(3);
	std::atomic<int> running = 0;
	std::atomic<int> done = 0;
	Hold hold;
	for (int i = 0; i < 3; i++)
	{
		stdexec::start_detached(stdexec::schedule(pool.scheduler()) | stdexec::then([&]() {
			running++;
			hold.wait();
			done++;
		}));
	}
	const bool allRunning = waitFor(running, 3, std::chrono::seconds(2));
	hold.release();

	REQUIRE(allRunning);
	REQUIRE(waitFor(done, 3, std::chrono::seconds(2)));
}

TEST_CASE("shared queue pool stops the work still queued when it goes away")
{
	std::atomic<int> ran = 0;
	std::atomic<int> stopped = 0;
	Hold hold;
	std::optional<std::thread> releaser;
	{
		execution::SharedQueuePool pool(1);
		stdexec::start_detached(stdexec::schedule(pool.scheduler()) | stdexec::then([&hold]() { hold.wait(); }));
		hold.waitUntilHeld();
		stdexec::start_detached(
			stdexec::schedule(pool.scheduler()) | stdexec::then([&ran]() { ran++; }) |
			stdexec::upon_stopped([&stopped]() { stopped++; }));
		// The held step ends once the pool is going away; the queued one never runs.
		releaser.emplace([&hold]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			hold.release();
		});
	}
	releaser->join();

	REQUIRE(ran == 0);
	REQUIRE(stopped == 1);
}
