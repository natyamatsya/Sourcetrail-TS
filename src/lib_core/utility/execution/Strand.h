#pragma once

//-----------------------------------------------------------------------
/*!
 * @file	Strand.h
 *
 * @brief	A serial execution context over a thread pool.
 *
 * What is posted to a strand runs one item at a time, in the order posted,
 * on the pool's threads; while nothing is posted the strand holds no thread.
 * Its scheduler() is a stdexec scheduler, so a pipeline that does its work
 * elsewhere (on io(), say) can continue on the strand and run serialized
 * with everything else posted there:
 *   @code
 *   stdexec::schedule(schedulers.io()) | stdexec::then(openIndex)
 *       | stdexec::continues_on(strand->scheduler()) | stdexec::then(install)
 *   @endcode
 */
//-----------------------------------------------------------------------

#include "SenderAliases.h"
#include "StdexecPrelude.h"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace execution
{
class Strand: public std::enable_shared_from_this<Strand>
{
public:
	class Scheduler;

	static std::shared_ptr<Strand> create(AnyScheduler pool);

	//! Runs `work` after what was posted before it, on a thread of the pool.
	void post(std::function<void()> work);

	//! Whether the calling thread is running this strand's work now.
	[[nodiscard]] bool runsHere() const noexcept;

	[[nodiscard]] Scheduler scheduler();

private:
	explicit Strand(AnyScheduler pool);

	void drain();

	AnyScheduler m_pool;
	std::mutex m_mutex;
	std::deque<std::function<void()>> m_queue;
	bool m_draining = false;	// a drain is scheduled on the pool or running
};

//! The stdexec scheduler of a strand: schedule() completes on it. Completes with
//! set_stopped when the receiver's stop was requested by the time it gets there.
class Strand::Scheduler
{
public:
	struct Sender
	{
		using sender_concept = stdexec::sender_t;
		using completion_signatures =
			stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>;

		std::shared_ptr<Strand> strand;

		struct Env
		{
			std::shared_ptr<Strand> strand;

			Scheduler query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept
			{
				return Scheduler {strand};
			}
			Scheduler query(stdexec::get_completion_scheduler_t<stdexec::set_stopped_t>) const noexcept
			{
				return Scheduler {strand};
			}
		};

		[[nodiscard]] Env get_env() const noexcept
		{
			return {strand};
		}

		template <class Receiver>
		struct Operation
		{
			Receiver receiver;
			std::shared_ptr<Strand> strand;

			void start() & noexcept
			{
				strand->post([this]() noexcept {
					if (stdexec::get_stop_token(stdexec::get_env(receiver)).stop_requested())
					{
						stdexec::set_stopped(std::move(receiver));
					}
					else
					{
						stdexec::set_value(std::move(receiver));
					}
				});
			}
		};

		template <class Receiver>
		Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) const noexcept
		{
			return {std::forward<Receiver>(receiver), strand};
		}
	};

	explicit Scheduler(std::shared_ptr<Strand> strand) noexcept: m_strand(std::move(strand)) {}

	[[nodiscard]] Sender schedule() const noexcept
	{
		return {m_strand};
	}

	[[nodiscard]] friend bool operator==(const Scheduler&, const Scheduler&) noexcept = default;

private:
	std::shared_ptr<Strand> m_strand;
};

static_assert(stdexec::scheduler<Strand::Scheduler>, "a strand's scheduler models stdexec::scheduler");
}	 // namespace execution
