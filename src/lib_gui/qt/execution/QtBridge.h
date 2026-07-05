#pragma once

// Qt sender/receiver bridge. Originally by Ville Voutilainen (libunifex-qt),
// ported from libunifex to NVIDIA stdexec.
//-----------------------------------------------------------------------
/*!
 * @file	QtBridge.h
 *
 * @brief	Umbrella header for the Qt <-> stdexec bridge.
 *
 * Include this to pull in:
 *   - @c QObjectDerived            concept                   (@c Concepts.h)
 *   - @c QtThreadScheduler         stdexec scheduler         (@c Scheduler.h)
 *   - @c UiScheduler()             convenience               (@c Scheduler.h)
 *   - @c SignalSender / FromSignal one-shot signal sender    (@c SignalSender.h)
 *   - @c FromQFuture / ToQFuture   QFuture <-> sender bridge (@c QFuture.h)
 *
 * For finer control over translation units, include the individual
 * headers directly.
 */
//-----------------------------------------------------------------------

#include "Concepts.h"
#include "QFuture.h"
#include "QtThreadScheduler.h"
#include "SignalSender.h"
