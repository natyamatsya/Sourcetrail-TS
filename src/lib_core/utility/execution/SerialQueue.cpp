#include "SerialQueue.h"

#include <exception>
#include <string>

#include "logging.h"

namespace execution
{
namespace
{
// How many items a turn runs before it gives the strand back (Strand::drain's
// reason): a queue that keeps getting work shares the pool with the others.
constexpr int itemsPerTurn = 16;

// Where an item stands: its run() returned, or it finished, or both.
enum class Phase
{
	Running,
	Finished,	// before run() returned
	Returned	// before it finished
};
}	 // namespace

std::shared_ptr<SerialQueue> SerialQueue::create(std::shared_ptr<Strand> strand)
{
	return std::shared_ptr<SerialQueue>(new SerialQueue(std::move(strand)));
}

std::shared_ptr<SerialQueue> SerialQueue::createWithOwnPool()
{
	// Never destroyed: its threads are idle once the queues on it stopped.
	static exec::static_thread_pool* const pool = new exec::static_thread_pool(4);
	return create(Strand::create(AnyScheduler(pool->get_scheduler())));
}

SerialQueue::SerialQueue(std::shared_ptr<Strand> strand): m_strand(std::move(strand)) {}

bool SerialQueue::push(Item item, bool front)
{
	Entry entry {std::move(item), currentCause()};
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		if (!m_stopped)
		{
			CauseWork::begun(entry.cause);
			front ? m_entries.push_front(std::move(entry)) : m_entries.push_back(std::move(entry));
			if (!std::exchange(m_busy, true))
			{
				lock.unlock();
				m_strand->post([self = shared_from_this()]() { self->turn(); });
			}
			return true;
		}
	}
	if (entry.item.dropped)
	{
		const CauseScope scope(entry.cause);
		entry.item.dropped();
	}
	return false;
}

bool SerialQueue::post(std::function<void()> work, bool front)
{
	return push(
		{[work = std::move(work)](std::function<void()> done) {
			 // Finished however it ends.
			 struct Done
			 {
				 std::function<void()>& done;
				 ~Done()
				 {
					 done();
				 }
			 } finished {done};
			 work();
		 },
		 {}},
		front);
}

void SerialQueue::stop()
{
	std::deque<Entry> dropped;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_stopped = true;
		dropped.swap(m_entries);
	}
	for (Entry& entry: dropped)
	{
		const CauseScope scope(entry.cause);
		if (entry.item.dropped)
		{
			entry.item.dropped();
		}
		CauseWork::ended(entry.cause);
	}
}

bool SerialQueue::runsHere() const noexcept
{
	return m_running && m_strand->runsHere();
}

SerialQueue::Scheduler SerialQueue::scheduler()
{
	return Scheduler(shared_from_this());
}

void SerialQueue::turn()
{
	for (int run = 0; run < itemsPerTurn; run++)
	{
		Entry entry;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			if (m_entries.empty())
			{
				m_busy = false;
				return;
			}
			entry = std::move(m_entries.front());
			m_entries.pop_front();
		}

		const auto phase = std::make_shared<std::atomic<Phase>>(Phase::Running);
		std::function<void()> done = [self = shared_from_this(), phase, cause = entry.cause]() {
			const Phase before = phase->exchange(Phase::Finished);
			if (before == Phase::Finished)
			{
				return;	// once
			}
			CauseWork::ended(cause);
			if (before == Phase::Returned)
			{
				// It finished after its turn gave the strand back: the next turn.
				self->m_strand->post([self]() { self->turn(); });
			}
		};

		{
			const CauseScope scope(entry.cause);
			m_running = true;
			try
			{
				entry.item.run(done);
			}
			catch (const std::exception& e)
			{
				logFailure((std::string("an item of a serial queue threw: ") + e.what()).c_str());
				done();
			}
			catch (...)
			{
				logFailure("an item of a serial queue threw an unknown exception");
				done();
			}
			m_running = false;
		}

		Phase expected = Phase::Running;
		if (phase->compare_exchange_strong(expected, Phase::Returned))
		{
			return;	// it goes on elsewhere, holding the queue; `done` posts the next turn
		}
	}
	// A batch has run: the rest in a turn posted anew.
	m_strand->post([self = shared_from_this()]() { self->turn(); });
}

void SerialQueue::logFailure(const char* what)
{
	LOG_ERROR(what);
}
}	 // namespace execution
