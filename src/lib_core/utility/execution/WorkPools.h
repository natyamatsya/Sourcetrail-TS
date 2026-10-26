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
 * thread once a delay has passed.
 */
//-----------------------------------------------------------------------

#include "SenderAliases.h"

#include <chrono>
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

//! Runs `work` on the timer's thread once `delay` has passed. Keep it short: post
//! what takes longer elsewhere.
void afterDelay(std::chrono::milliseconds delay, std::function<void()> work);
}	 // namespace execution
