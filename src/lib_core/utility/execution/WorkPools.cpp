#include "WorkPools.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <exec/timed_thread_scheduler.hpp>

#include "MessageCause.h"
#include "StdexecPrelude.h"
#include "logging.h"
#include "utilityApp.h"

namespace execution
{
namespace
{
std::mutex s_poolMutex;
std::optional<AnyScheduler> s_workPool;
}	 // namespace

AnyScheduler workPool()
{
	const std::lock_guard<std::mutex> lock(s_poolMutex);
	return s_workPool ? *s_workPool : fallbackPool();
}

void setWorkPool(std::optional<AnyScheduler> pool)
{
	const std::lock_guard<std::mutex> lock(s_poolMutex);
	s_workPool = std::move(pool);
}

AnyScheduler fallbackPool()
{
	// Never destroyed: its threads are idle once what runs on it ended.
	static exec::static_thread_pool* const pool = new exec::static_thread_pool(4);
	return AnyScheduler(pool->get_scheduler());
}

exec::timed_thread_scheduler timerScheduler()
{
	// Never destroyed: a wait still pending at exit is not waited for.
	static exec::timed_thread_context* const timer = new exec::timed_thread_context();
	return timer->get_scheduler();
}

void afterDelay(std::chrono::milliseconds delay, std::function<void()> work)
{
	stdexec::start_detached(exec::schedule_after(timerScheduler(), delay) | stdexec::then(std::move(work)));
}

void runOnWorkPool(std::function<void()> work)
{
	stdexec::start_detached(
		stdexec::schedule(workPool()) | stdexec::then(withCurrentCause([work = std::move(work)]() {
			try
			{
				work();
			}
			catch (const std::exception& e)
			{
				LOG_ERROR(std::string("work on the work pool failed: ") + e.what());
			}
			catch (...)
			{
				LOG_ERROR("work on the work pool failed with an unknown exception");
			}
		})));
}

void parallelFor(std::size_t count, const std::function<void(std::size_t)>& part)
{
	struct Shared
	{
		std::atomic<std::size_t> next = 0;
		std::mutex mutex;
		std::size_t remaining = 0;
		std::condition_variable ended;
		std::exception_ptr failure;
	};
	const auto shared = std::make_shared<Shared>();
	shared->remaining = count;

	// Takes parts until none is left. `part` is the caller's, alive while parts remain:
	// a helper that comes late takes none and touches nothing of it.
	const auto run = [shared, partPointer = &part, count]() {
		for (std::size_t index = shared->next++; index < count; index = shared->next++)
		{
			try
			{
				(*partPointer)(index);
			}
			catch (...)
			{
				const std::lock_guard<std::mutex> lock(shared->mutex);
				if (!shared->failure)
				{
					shared->failure = std::current_exception();
				}
			}
			bool last = false;
			{
				const std::lock_guard<std::mutex> lock(shared->mutex);
				last = --shared->remaining == 0;
			}
			if (last)
			{
				shared->ended.notify_all();
			}
		}
	};

	const std::size_t helpers =
		std::min<std::size_t>(count > 0 ? count - 1 : 0, static_cast<std::size_t>(utility::getIdealThreadCount()));
	for (std::size_t helper = 0; helper < helpers; helper++)
	{
		stdexec::start_detached(stdexec::schedule(workPool()) | stdexec::then(withCurrentCause(run)));
	}
	run();

	std::unique_lock<std::mutex> lock(shared->mutex);
	shared->ended.wait(lock, [&shared]() { return shared->remaining == 0; });
	if (shared->failure)
	{
		std::rethrow_exception(shared->failure);
	}
}
}	 // namespace execution
