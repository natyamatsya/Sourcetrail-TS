#include "MessageCause.h"

#include <mutex>
#include <unordered_map>

namespace
{
thread_local std::uint64_t t_cause = 0;

struct PendingWork
{
	std::mutex mutex;
	std::unordered_map<std::uint64_t, std::size_t> counts;
	std::function<void(std::uint64_t)> done;
};

PendingWork& pendingWork()
{
	static PendingWork work;
	return work;
}
}	 // namespace

CauseScope::CauseScope(std::uint64_t cause): m_previous(t_cause)
{
	t_cause = cause;
}

CauseScope::~CauseScope()
{
	t_cause = m_previous;
}

std::uint64_t currentCause()
{
	return t_cause;
}

void CauseWork::begun(std::uint64_t cause)
{
	if (cause == 0)
	{
		return;	// nobody waits for work on behalf of nothing
	}
	PendingWork& work = pendingWork();
	const std::lock_guard<std::mutex> lock(work.mutex);
	work.counts[cause]++;
}

void CauseWork::ended(std::uint64_t cause)
{
	if (cause == 0)
	{
		return;
	}
	PendingWork& work = pendingWork();
	std::function<void(std::uint64_t)> done;
	{
		const std::lock_guard<std::mutex> lock(work.mutex);
		const auto it = work.counts.find(cause);
		if (it == work.counts.end() || --it->second > 0)
		{
			return;
		}
		work.counts.erase(it);
		done = work.done;
	}
	if (done)
	{
		done(cause);
	}
}

std::size_t CauseWork::pending(std::uint64_t cause)
{
	PendingWork& work = pendingWork();
	const std::lock_guard<std::mutex> lock(work.mutex);
	const auto it = work.counts.find(cause);
	return it != work.counts.end() ? it->second : 0;
}

void CauseWork::onDone(std::function<void(std::uint64_t)> done)
{
	PendingWork& work = pendingWork();
	const std::lock_guard<std::mutex> lock(work.mutex);
	work.done = std::move(done);
}
