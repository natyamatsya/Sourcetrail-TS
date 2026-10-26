#ifndef TASK_SENDER_H
#define TASK_SENDER_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <exec/timed_thread_scheduler.hpp>

#include "MessageCause.h"
#include "StdexecPrelude.h"
#include "Strand.h"
#include "Task.h"
#include "TaskRunner.h"
#include "WorkPools.h"

class Blackboard;

namespace execution
{
//! What runs one task (a step of the indexing pipeline) to its end, shared by the
//! sender's operation and the turns it posts: they may come after the operation
//! completed (a late wake), and then do nothing.
class TaskDriver: public std::enable_shared_from_this<TaskDriver>
{
public:
	//! How the task ended; empty when it was stopped.
	using Completion = std::function<void(std::optional<Task::TaskState>)>;

	TaskDriver(std::shared_ptr<Task> task, std::shared_ptr<Blackboard> blackboard, bool reset, std::uint64_t cause);

	//! What is called once the task ended (once, from its strand); before begin().
	void arm(Completion completion);
	//! Starts its turns.
	void begin();
	//! Terminates the task and completes as stopped, in the task's turn.
	void stop();

private:
	// On the strand: updates the task a few times, until it waits parked or ends.
	void turn();
	void finish(std::optional<Task::TaskState> state);

	std::shared_ptr<TaskRunner> m_runner;	// on the strand only; released as it finishes
	const std::shared_ptr<Strand> m_strand;
	std::shared_ptr<Blackboard> m_blackboard;	// likewise
	const std::uint64_t m_cause;
	Completion m_completion;	// on the strand only
	bool m_finished = false;	// on the strand only
};

//! Runs `task` with `blackboard` to its end, as a sender of how it ended
//! (STATE_SUCCESS or STATE_FAILURE; a throw is a failure), on a strand of its own over
//! the work pool: its updates in turns, a few at a time, and while it waits parked
//! (TaskParking::parkCurrentTask, restFor) it holds no thread. `reset`: it ran
//! before and runs again. On behalf of the cause current as it is made. Stopping it
//! terminates the task: it completes with set_stopped.
class TaskSender
{
public:
	using sender_concept = stdexec::sender_t;
	using completion_signatures =
		stdexec::completion_signatures<stdexec::set_value_t(Task::TaskState), stdexec::set_stopped_t()>;

	TaskSender(std::shared_ptr<Task> task, std::shared_ptr<Blackboard> blackboard, bool reset)
		: m_task(std::move(task)), m_blackboard(std::move(blackboard)), m_reset(reset), m_cause(currentCause())
	{
	}

	template <class Receiver>
	struct Operation
	{
		struct OnStop
		{
			std::shared_ptr<TaskDriver> driver;
			void operator()() const noexcept
			{
				driver->stop();
			}
		};
		using StopToken = stdexec::stop_token_of_t<stdexec::env_of_t<Receiver>>;

		Receiver receiver;
		std::shared_ptr<TaskDriver> driver;
		std::optional<typename StopToken::template callback_type<OnStop>> onStop;

		void start() & noexcept
		{
			const StopToken token = stdexec::get_stop_token(stdexec::get_env(receiver));
			if (token.stop_requested())
			{
				stdexec::set_stopped(std::move(receiver));
				return;
			}
			// The completion first, then the stop callback, then the turns: a stop
			// that came meanwhile finds the completion, and the completion (which
			// resets the callback) cannot run while the callback is being set up.
			driver->arm([this](std::optional<Task::TaskState> state) {
				onStop.reset();
				if (state)
				{
					stdexec::set_value(std::move(receiver), *state);
				}
				else
				{
					stdexec::set_stopped(std::move(receiver));
				}
			});
			onStop.emplace(token, OnStop {driver});
			driver->begin();
		}
	};

	template <class Receiver>
	Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) && noexcept
	{
		return {
			std::forward<Receiver>(receiver),
			std::make_shared<TaskDriver>(std::move(m_task), std::move(m_blackboard), m_reset, m_cause),
			std::nullopt};
	}

private:
	std::shared_ptr<Task> m_task;
	std::shared_ptr<Blackboard> m_blackboard;
	bool m_reset;
	std::uint64_t m_cause;
};

//! See TaskSender.
inline TaskSender runTask(std::shared_ptr<Task> task, std::shared_ptr<Blackboard> blackboard, bool reset = false)
{
	return TaskSender(std::move(task), std::move(blackboard), reset);
}

//! Completes on the work pool once `delay` has passed (a pipeline's poll), holding no
//! thread meanwhile.
inline auto delay(std::chrono::milliseconds delay)
{
	return exec::schedule_after(timerScheduler(), delay) | stdexec::continues_on(workPool());
}
}	 // namespace execution

#endif	  // TASK_SENDER_H
