#pragma once

//-----------------------------------------------------------------------
/*!
 * @file	SerialQueue.h
 *
 * @brief	Work that runs one item at a time, in the order queued.
 *
 * An item may finish later than it returns: a parallel message's listeners
 * run side by side on the compute pool while the item waits for them. The
 * queue then starts the next item only once that one has finished, so the
 * item holds the queue, not a thread. The items run on a strand, beside what
 * else is posted there.
 *
 * Every item counts as the work of the cause it was queued under
 * (CauseWork) until it finished, or was dropped unrun by stop(), and runs
 * under that cause.
 *
 * Its scheduler() is a stdexec scheduler: what follows schedule() runs as an
 * item, in its turn, and holds the queue until it goes elsewhere or ends:
 *   @code
 *   stdexec::schedule(queue->scheduler()) | stdexec::then(work)
 *   @endcode
 */
//-----------------------------------------------------------------------

#include "MessageCause.h"
#include "StdexecPrelude.h"
#include "Strand.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace execution
{
class SerialQueue: public std::enable_shared_from_this<SerialQueue>
{
public:
	class Scheduler;

	//! An item: `run(done)` starts it in its turn; it calls `done()` once it has
	//! finished, from any thread, before or after it returns. `dropped()`, when set,
	//! is called instead for an item stop() drops unrun.
	struct Item
	{
		std::move_only_function<void(std::function<void()> done)> run;
		std::move_only_function<void()> dropped;
	};

	//! A queue whose items run on `strand`.
	static std::shared_ptr<SerialQueue> create(std::shared_ptr<Strand> strand);

	//! A queue on a strand of its own over the fallback pool (WorkPools.h), for
	//! queues with no pool to share (headless, tests).
	static std::shared_ptr<SerialQueue> createWithOwnPool();

	//! Queues `item` after what is queued (`front`: before it), on behalf of the
	//! current cause. False once stopped: `item` is dropped (`dropped` called).
	bool push(Item item, bool front = false);

	//! Queues `work`: an item that has finished when it returns.
	bool post(std::function<void()> work, bool front = false);

	//! Queues `sender`, started in its turn: an item that has finished once the
	//! sender completed (an error is logged).
	template <stdexec::sender Sender>
	bool spawn(Sender sender, std::function<void()> dropped = {});

	//! Drops what is queued; an item that runs finishes. Nothing is queued from here
	//! on.
	void stop();

	//! Whether the calling thread is running one of this queue's items now.
	[[nodiscard]] bool runsHere() const noexcept;

	[[nodiscard]] Scheduler scheduler();

private:
	explicit SerialQueue(std::shared_ptr<Strand> strand);

	struct Entry
	{
		Item item;
		std::uint64_t cause = 0;
	};

	//! On the strand: runs the items that finish as they return, until one does
	//! not (it calls for the next turn when it finishes) or a batch has run.
	void turn();

	static void logFailure(const char* what);

	const std::shared_ptr<Strand> m_strand;
	std::mutex m_mutex;
	std::deque<Entry> m_entries;
	bool m_busy = false;	// a turn is posted or runs, or an item has not finished
	bool m_stopped = false;
	std::atomic<bool> m_running = false;	// an item's run() is on the stack
};

//! The stdexec scheduler of a serial queue: schedule() completes as an item of
//! the queue, in its turn; set_stopped when the receiver's stop was requested by
//! then, or the queue dropped it.
class SerialQueue::Scheduler
{
public:
	struct Sender
	{
		using sender_concept = stdexec::sender_t;
		using completion_signatures =
			stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>;

		std::shared_ptr<SerialQueue> queue;

		struct Env
		{
			std::shared_ptr<SerialQueue> queue;

			Scheduler query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept
			{
				return Scheduler {queue};
			}
			Scheduler query(stdexec::get_completion_scheduler_t<stdexec::set_stopped_t>) const noexcept
			{
				return Scheduler {queue};
			}
		};

		[[nodiscard]] Env get_env() const noexcept
		{
			return {queue};
		}

		template <class Receiver>
		struct Operation
		{
			Receiver receiver;
			std::shared_ptr<SerialQueue> queue;

			void start() & noexcept
			{
				queue->push(
					{[this](std::function<void()> done) {
						 // The receiver's completion may end this operation: `done`
						 // is the call's own.
						 if (stdexec::get_stop_token(stdexec::get_env(receiver)).stop_requested())
						 {
							 stdexec::set_stopped(std::move(receiver));
						 }
						 else
						 {
							 stdexec::set_value(std::move(receiver));
						 }
						 done();
					 },
					 [this]() { stdexec::set_stopped(std::move(receiver)); }});
			}
		};

		template <class Receiver>
		Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) const noexcept
		{
			return {std::forward<Receiver>(receiver), queue};
		}
	};

	explicit Scheduler(std::shared_ptr<SerialQueue> queue) noexcept: m_queue(std::move(queue)) {}

	[[nodiscard]] Sender schedule() const noexcept
	{
		return {m_queue};
	}

	[[nodiscard]] friend bool operator==(const Scheduler&, const Scheduler&) noexcept = default;

private:
	std::shared_ptr<SerialQueue> m_queue;
};

static_assert(stdexec::scheduler<SerialQueue::Scheduler>, "a serial queue's scheduler models stdexec::scheduler");

template <stdexec::sender Sender>
bool SerialQueue::spawn(Sender sender, std::function<void()> dropped)
{
	return push(
		{[sender = std::move(sender)](std::function<void()> done) mutable {
			 stdexec::start_detached(
				 std::move(sender) | stdexec::then([done](auto&&...) noexcept { done(); }) |
				 stdexec::upon_error([done](auto&&) noexcept {
					 logFailure("an item of a serial queue failed");
					 done();
				 }) |
				 stdexec::upon_stopped([done]() noexcept { done(); }));
		 },
		 [dropped = std::move(dropped)]() mutable {
			 if (dropped)
			 {
				 dropped();
			 }
		 }});
}
}	 // namespace execution
