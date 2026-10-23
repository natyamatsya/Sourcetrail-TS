// Module build: LOG_* macros stay textual; backend via `import srctrl.logging` below.
#ifdef SRCTRL_MODULE_BUILD
#define SRCTRL_LOGGING_VIA_IMPORT
#endif

#include "Strand.h"

#include <exception>
#include <string>

#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.logging;
#endif

namespace execution
{
namespace
{
// The strand whose work the calling thread runs now, if any.
thread_local const Strand* t_running = nullptr;
}	 // namespace

std::shared_ptr<Strand> Strand::create(AnyScheduler pool)
{
	return std::shared_ptr<Strand>(new Strand(std::move(pool)));
}

Strand::Strand(AnyScheduler pool): m_pool(std::move(pool)) {}

void Strand::post(std::function<void()> work)
{
	bool startDrain = false;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_queue.push_back(std::move(work));
		startDrain = !std::exchange(m_draining, true);
	}
	if (startDrain)
	{
		stdexec::start_detached(
			stdexec::schedule(m_pool) | stdexec::then([self = shared_from_this()]() { self->drain(); }));
	}
}

bool Strand::runsHere() const noexcept
{
	return t_running == this;
}

Strand::Scheduler Strand::scheduler()
{
	return Scheduler(shared_from_this());
}

void Strand::drain()
{
	// A batch at a time, then the pool's thread goes back to the pool: a strand that
	// keeps getting work (a task holding, polled each pass) shares the pool with the
	// other strands instead of keeping a thread to itself.
	constexpr int batch = 16;
	const Strand* const outer = std::exchange(t_running, this);
	for (int done = 0;; done++)
	{
		std::function<void()> work;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			if (m_queue.empty())
			{
				m_draining = false;
				break;
			}
			if (done == batch)
			{
				// Still draining: the rest goes on in a drain scheduled anew.
				stdexec::start_detached(
					stdexec::schedule(m_pool) | stdexec::then([self = shared_from_this()]() { self->drain(); }));
				break;
			}
			work = std::move(m_queue.front());
			m_queue.pop_front();
		}
		try
		{
			work();
		}
		catch (const std::exception& e)
		{
			LOG_ERROR(std::string("strand: work ended with an exception: ") + e.what());
		}
		catch (...)
		{
			LOG_ERROR("strand: work ended with an unknown exception");
		}
	}
	t_running = outer;
}
}	 // namespace execution
