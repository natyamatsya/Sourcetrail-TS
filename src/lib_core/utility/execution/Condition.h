#pragma once

//-----------------------------------------------------------------------
/*!
 * @file	Condition.h
 *
 * @brief	Waiting, holding no thread, until what other threads bring about holds.
 *
 * A coroutine waits for something other threads change (a storage to merge, a
 * decision, a stage that began) with `co_await condition.until(ready)`; whoever
 * changes what `ready` reads calls notify() afterwards. `ready` is checked as the
 * wait starts and again after each notify(), under the condition's lock, so a
 * notify() between the check and the wait is not missed.
 */
//-----------------------------------------------------------------------

#include <atomic>
#include <concepts>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "StdexecPrelude.h"

namespace execution
{
class Condition
{
	struct Waiter
	{
		virtual ~Waiter() = default;
		virtual bool ready() = 0;
		// Removed from the waiters by the caller, under the lock: it completes, once.
		virtual void complete(bool stopped) noexcept = 0;
	};

public:
	//! A sender that completes once `ready()` holds; stopping it completes it with
	//! set_stopped. `ready` runs under the condition's lock, on the thread that
	//! starts the wait or calls notify(): keep it short, and do not notify from it.
	template <std::invocable Ready>
	auto until(Ready ready);

	//! From any thread, after what the waits' `ready` read has changed.
	void notify()
	{
		std::vector<Waiter*> completing;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			std::erase_if(m_waiters, [&completing](Waiter* waiter) {
				if (!waiter->ready())
				{
					return false;
				}
				completing.push_back(waiter);
				return true;
			});
		}
		for (Waiter* waiter: completing)
		{
			waiter->complete(false);
		}
	}

private:
	template <class Ready>
	class Sender;

	std::mutex m_mutex;
	std::vector<Waiter*> m_waiters;	// guarded by m_mutex
};

template <class Ready>
class Condition::Sender
{
public:
	using sender_concept = stdexec::sender_t;
	using completion_signatures = stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>;

	Sender(Condition& condition, Ready ready): m_condition(&condition), m_ready(std::move(ready)) {}

	template <class Receiver>
	class Operation: public Waiter
	{
	public:
		Operation(Condition& condition, Ready ready, Receiver receiver)
			: m_condition(condition), m_ready(std::move(ready)), m_receiver(std::move(receiver))
		{
		}
		Operation(const Operation&) = delete;
		Operation& operator=(const Operation&) = delete;

		void start() & noexcept
		{
			const auto token = stdexec::get_stop_token(stdexec::get_env(m_receiver));
			if (token.stop_requested())
			{
				stdexec::set_stopped(std::move(m_receiver));
				return;
			}
			{
				const std::lock_guard<std::mutex> lock(m_condition.m_mutex);
				if (!m_ready())
				{
					m_condition.m_waiters.push_back(this);
					m_waiting = true;
				}
			}
			if (!m_waiting)
			{
				stdexec::set_value(std::move(m_receiver));
				return;
			}
			// A notify() or a stop may complete it from here on, also while the stop
			// callback is set up: that completion is delivered once start is done.
			m_onStop.emplace(token, OnStop {this});
			if (m_state.fetch_and(~Starting) & Completed)
			{
				deliver();
			}
		}

	private:
		struct OnStop
		{
			Operation* operation;
			void operator()() const noexcept
			{
				bool removed = false;
				{
					const std::lock_guard<std::mutex> lock(operation->m_condition.m_mutex);
					removed = std::erase(operation->m_condition.m_waiters, operation) > 0;
				}
				if (removed)
				{
					operation->complete(true);
				}
			}
		};
		using StopToken = stdexec::stop_token_of_t<stdexec::env_of_t<Receiver>>;
		using StopCallback = typename StopToken::template callback_type<OnStop>;

		enum : int
		{
			Starting = 1,
			Completed = 2
		};

		bool ready() override
		{
			return m_ready();
		}

		void complete(bool stopped) noexcept override
		{
			m_stopped = stopped;
			if (m_state.fetch_or(Completed) & Starting)
			{
				return;	// start delivers it
			}
			deliver();
		}

		void deliver() noexcept
		{
			m_onStop.reset();
			if (m_stopped)
			{
				stdexec::set_stopped(std::move(m_receiver));
			}
			else
			{
				stdexec::set_value(std::move(m_receiver));
			}
		}

		Condition& m_condition;
		Ready m_ready;
		Receiver m_receiver;
		bool m_waiting = false;
		bool m_stopped = false;
		std::atomic<int> m_state = Starting;
		std::optional<StopCallback> m_onStop;
	};

	template <class Receiver>
	Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) && noexcept
	{
		return {*m_condition, std::move(m_ready), std::forward<Receiver>(receiver)};
	}

private:
	Condition* m_condition;
	Ready m_ready;
};

template <std::invocable Ready>
auto Condition::until(Ready ready)
{
	return Sender<Ready>(*this, std::move(ready));
}
}	 // namespace execution
