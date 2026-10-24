#pragma once

//-----------------------------------------------------------------------
/*!
 * @file	SharedQueuePool.h
 *
 * @brief	A thread pool whose threads all take work from one queue.
 *
 * For work that blocks: a strand's task step that waits on a child process, a
 * file read, a subprocess's IPC. Whatever is scheduled runs on the first thread
 * that is free, so one thread held by a long step never holds up work behind
 * it while the others are idle.
 *
 * exec::static_thread_pool is the wrong tool for that. Work scheduled from
 * outside it (thoth-ipc's reactor thread, the Qt GUI thread) lands in the
 * remote slot of one thread picked at random, and only that thread takes it:
 * the others steal from local queues only. With one thread blocked, a quarter
 * of a four-thread pool's remote work waited for it, however long it blocked.
 */
//-----------------------------------------------------------------------

#include "StdexecPrelude.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace execution
{
class SharedQueuePool
{
public:
	class Scheduler;

	//! Starts @p threadCount threads (at least one).
	explicit SharedQueuePool(unsigned threadCount);

	//! Stops the threads once they finish what they run; work still queued
	//! completes with set_stopped.
	~SharedQueuePool();

	SharedQueuePool(const SharedQueuePool&) = delete;
	SharedQueuePool& operator=(const SharedQueuePool&) = delete;

	[[nodiscard]] Scheduler scheduler() noexcept;

private:
	// An operation state waiting in the queue: run() on a pool thread, or
	// stop() when the pool goes away first.
	struct Task
	{
		Task* next = nullptr;
		void (*run)(Task*) noexcept = nullptr;
		void (*stop)(Task*) noexcept = nullptr;
	};

	void push(Task* task);
	void work();

	std::mutex m_mutex;
	std::condition_variable m_wake;
	Task* m_head = nullptr;
	Task* m_tail = nullptr;
	bool m_stopping = false;
	std::vector<std::thread> m_threads;
};

//! The stdexec scheduler of a SharedQueuePool. Completes with set_stopped when
//! the receiver's stop was requested by the time a thread gets to it.
class SharedQueuePool::Scheduler
{
public:
	struct Sender
	{
		using sender_concept = stdexec::sender_t;
		using completion_signatures =
			stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>;

		SharedQueuePool* pool;

		struct Env
		{
			SharedQueuePool* pool;

			Scheduler query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept
			{
				return Scheduler {pool};
			}
			Scheduler query(stdexec::get_completion_scheduler_t<stdexec::set_stopped_t>) const noexcept
			{
				return Scheduler {pool};
			}
		};

		[[nodiscard]] Env get_env() const noexcept
		{
			return {pool};
		}

		template <class Receiver>
		struct Operation: Task
		{
			Receiver receiver;
			SharedQueuePool* pool;

			Operation(Receiver r, SharedQueuePool* p) noexcept: receiver(std::move(r)), pool(p)
			{
				this->run = [](Task* task) noexcept {
					auto& self = *static_cast<Operation*>(task);
					if (stdexec::get_stop_token(stdexec::get_env(self.receiver)).stop_requested())
					{
						stdexec::set_stopped(std::move(self.receiver));
					}
					else
					{
						stdexec::set_value(std::move(self.receiver));
					}
				};
				this->stop = [](Task* task) noexcept {
					stdexec::set_stopped(std::move(static_cast<Operation*>(task)->receiver));
				};
			}

			Operation(Operation&&) = delete;

			void start() & noexcept
			{
				pool->push(this);
			}
		};

		template <class Receiver>
		Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) const noexcept
		{
			return {std::forward<Receiver>(receiver), pool};
		}
	};

	explicit Scheduler(SharedQueuePool* pool) noexcept: m_pool(pool) {}

	[[nodiscard]] Sender schedule() const noexcept
	{
		return {m_pool};
	}

	[[nodiscard]] friend bool operator==(const Scheduler&, const Scheduler&) noexcept = default;

private:
	SharedQueuePool* m_pool;
};

static_assert(stdexec::scheduler<SharedQueuePool::Scheduler>, "a SharedQueuePool's scheduler models stdexec::scheduler");
}	 // namespace execution
