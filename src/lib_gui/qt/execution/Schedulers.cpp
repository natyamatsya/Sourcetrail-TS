#include "Schedulers.h"

#include "QtThreadScheduler.h"	// UiScheduler()
#include "StdexecPrelude.h"		// exec::static_thread_pool
#include "logging.h"

#include <thread>

namespace execution::qt
{
struct Schedulers::Impl
{
	exec::static_thread_pool ioPool;
	exec::static_thread_pool computePool;

	Impl(unsigned ioThreads, unsigned computeThreads)
		: ioPool{ ioThreads }
		, computePool{ computeThreads }
	{
	}
};

namespace
{
// "0" requested -> hardware_concurrency, floored at 2 so we never degenerate to
// a single thread when the runtime reports an unknown topology.
unsigned defaultComputeThreads(unsigned requested) noexcept
{
	if (requested != 0)
		return requested;
	const unsigned hc = std::thread::hardware_concurrency();
	return hc == 0 ? 2u : hc;
}
}	 // namespace

Schedulers& Schedulers::getInstance()
{
	static Schedulers s_instance(2, 0);
	return s_instance;
}

Schedulers::Schedulers(unsigned ioThreads, unsigned computeThreads)
	: m_impl{ std::make_unique<Impl>(ioThreads == 0 ? 2u : ioThreads, defaultComputeThreads(computeThreads)) }
{
	LOG_INFO(
		"Schedulers: started with " + std::to_string(ioThreads == 0 ? 2u : ioThreads) + " I/O threads, " +
		std::to_string(defaultComputeThreads(computeThreads)) + " compute threads");
}

Schedulers::~Schedulers()
{
	LOG_INFO("Schedulers: stopping thread pools");
}

AnyScheduler Schedulers::io() const
{
	return m_impl->ioPool.get_scheduler();
}

AnyScheduler Schedulers::compute() const
{
	return m_impl->computePool.get_scheduler();
}

AnyScheduler Schedulers::ui() const
{
	// QtThreadScheduler is stateless apart from a QPointer to qApp, so building
	// one per call is cheap and tracks the application's lifetime.
	return UiScheduler();
}
}	 // namespace execution::qt
