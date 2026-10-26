#pragma once

//-----------------------------------------------------------------------
/*!
 * @file	TabQueues.h
 *
 * @brief	The serial queues messages and posted work run on, by tab id.
 *
 * Each id a message or a piece of work is scheduled on (TabIds::app(), the
 * background, each tab) has a SerialQueue: a message's listeners run there as
 * one item (MessageQueue), and so does work posted there. The indexer's task
 * trees keep the app's TaskScheduler.
 */
//-----------------------------------------------------------------------

#include "SerialQueue.h"
#include "TabIds.h"

#include <chrono>
#include <functional>
#include <memory>

namespace execution
{
//! Starts `tab`'s queue on `strand` (or, without one, on a pool of its own), in
//! place of one it had.
std::shared_ptr<SerialQueue> startQueue(TabId tab, std::shared_ptr<Strand> strand);

//! `tab`'s queue; empty when it has none (a tab that closed).
std::shared_ptr<SerialQueue> queueOf(TabId tab);

//! Stops `tab`'s queue and forgets it: what is queued there is dropped.
void stopQueue(TabId tab);

//! Stops every queue.
void stopQueues();

//! Runs `work` in `tab`'s queue in its turn, on behalf of the current cause; `front`:
//! before what is queued. Dropped, with a note in the log, when `tab` has no queue.
void post(TabId tab, std::function<void()> work, bool front = false);

//! Posts `work` to `tab`'s queue once `delay` has passed. The wait is no request's
//! work: nothing waits for it to settle.
void postAfter(TabId tab, std::chrono::milliseconds delay, std::function<void()> work);
}	 // namespace execution
