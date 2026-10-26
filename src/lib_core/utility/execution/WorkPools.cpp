#include "WorkPools.h"

#include <mutex>
#include <utility>

#include <exec/timed_thread_scheduler.hpp>

#include "StdexecPrelude.h"

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

void afterDelay(std::chrono::milliseconds delay, std::function<void()> work)
{
	// Never destroyed: a wait still pending at exit is not waited for.
	static exec::timed_thread_context* const timer = new exec::timed_thread_context();
	stdexec::start_detached(exec::schedule_after(timer->get_scheduler(), delay) | stdexec::then(std::move(work)));
}
}	 // namespace execution
