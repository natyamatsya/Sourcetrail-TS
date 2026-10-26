#pragma once

//-----------------------------------------------------------------------
/*!
 * @file	WorkPools.h
 *
 * @brief	Where work runs that has no scheduler of its own, and a timer.
 *
 * The work pool is the app's compute pool once the app set it (the indexer's
 * parallel streams run there); before, and without one (headless, tests), a
 * small pool kept for the purpose. afterDelay() runs a function on a timer
 * thread once a delay has passed; delay() is the same as a sender.
 */
//-----------------------------------------------------------------------

#include "SenderAliases.h"

#include <exec/timed_thread_scheduler.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>

namespace execution
{
//! The pool for work with no scheduler of its own: the app's compute pool, or the
//! fallback pool.
AnyScheduler workPool();

//! Sets the work pool (the app: its compute pool); empty for the fallback.
void setWorkPool(std::optional<AnyScheduler> pool);

//! A small pool kept for what has no pool to share (headless, tests): four threads,
//! never destroyed.
AnyScheduler fallbackPool();

//! The shared timer's scheduler: exec::schedule_after on it completes on its thread.
exec::timed_thread_scheduler timerScheduler();

//! Completes on the work pool once `delay` has passed (a coroutine's poll), holding no
//! thread meanwhile; stopped, it completes with set_stopped at once.
inline auto delay(std::chrono::milliseconds delay)
{
	return exec::schedule_after(timerScheduler(), delay) | stdexec::continues_on(workPool());
}

//! Runs `work` on the timer's thread once `delay` has passed. Keep it short: post
//! what takes longer elsewhere.
void afterDelay(std::chrono::milliseconds delay, std::function<void()> work);

//! Runs `work` on the work pool, on behalf of the current cause; what it throws is
//! logged.
void runOnWorkPool(std::function<void()> work);

//! Runs part(0) .. part(count - 1) side by side on the work pool and returns once
//! all ran; the first exception one threw is rethrown then. The calling thread takes
//! parts too, so it gets through even when no pool thread is free (a caller on the
//! pool itself).
void parallelFor(std::size_t count, const std::function<void(std::size_t)>& part);
}	 // namespace execution
