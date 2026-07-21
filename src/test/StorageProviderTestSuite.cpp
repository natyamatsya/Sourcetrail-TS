#include "Catch2.hpp"

#include <atomic>
#include <thread>

#ifndef SRCTRL_MODULE_BUILD
#include "IntermediateStorage.h"
#endif
#include "StorageProvider.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.storage;
#endif

// Tests for the event-driven StorageProvider wait/notify API that replaced the
// fixed-interval polling of the merge/inject tasks.

namespace
{
std::shared_ptr<IntermediateStorage> makeStorage()
{
	return std::make_shared<IntermediateStorage>();
}
}	 // namespace

TEST_CASE("storage provider wait returns ready when count already sufficient")
{
	StorageProvider provider;
	provider.insert(makeStorage());

	REQUIRE(provider.waitForCountOrDone(1) == StorageProvider::WaitResult::READY);
	REQUIRE(provider.getStorageCount() == 1);
}

TEST_CASE("storage provider insert wakes a blocked waiter")
{
	StorageProvider provider;
	std::atomic<bool> woke = false;

	std::thread waiter([&]() {
		if (provider.waitForCountOrDone(1) == StorageProvider::WaitResult::READY)
		{
			woke = true;
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	REQUIRE(!woke.load());

	provider.insert(makeStorage());
	waiter.join();

	REQUIRE(woke.load());
}

TEST_CASE("storage provider setDone releases waiters with DONE")
{
	StorageProvider provider;
	std::atomic<bool> done = false;

	std::thread waiter([&]() {
		if (provider.waitForCountOrDone(3) == StorageProvider::WaitResult::DONE)
		{
			done = true;
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	provider.setDone();
	waiter.join();

	REQUIRE(done.load());
	REQUIRE(provider.isDone());
}

TEST_CASE("storage provider done with enough storages still reports ready")
{
	StorageProvider provider;
	provider.insert(makeStorage());
	provider.setDone();

	// The remainder must still be drained after producers finish.
	REQUIRE(provider.waitForCountOrDone(1) == StorageProvider::WaitResult::READY);
}

TEST_CASE("storage provider concurrent producers lose no storages")
{
	StorageProvider provider;
	constexpr int STORAGES_PER_PRODUCER = 25;

	std::atomic<int> consumed = 0;
	std::thread consumer([&]() {
		while (provider.waitForCountOrDone(1) == StorageProvider::WaitResult::READY)
		{
			if (provider.consumeLargestStorage())
			{
				consumed++;
			}
		}
	});

	std::thread producerA([&]() {
		for (int i = 0; i < STORAGES_PER_PRODUCER; i++)
		{
			provider.insert(makeStorage());
		}
	});
	std::thread producerB([&]() {
		for (int i = 0; i < STORAGES_PER_PRODUCER; i++)
		{
			provider.insert(makeStorage());
		}
	});

	producerA.join();
	producerB.join();
	provider.setDone();
	consumer.join();

	REQUIRE(2 * STORAGES_PER_PRODUCER == consumed.load());
	REQUIRE(provider.getStorageCount() == 0);
}
