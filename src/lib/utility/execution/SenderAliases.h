#pragma once

//-----------------------------------------------------------------------
/*!
 * @file	SenderAliases.h
 *
 * @brief	Friendly aliases for stdexec's type-erased sender/scheduler forms.
 *
 * If a future stdexec revision moves the underlying spellings, this is the
 * single header to update. Interfaces that expose
 * senders should refer to @c execution::AnySenderOf and
 * @c execution::AnyScheduler instead of spelling the type-erased
 * forms inline.
 */
//-----------------------------------------------------------------------

#include "StdexecPrelude.h"

namespace execution
{

//! @c any_sender_of<set_value_t(T), set_error_t(E), set_stopped_t()> — a
//! type-erased sender carrying the given completion signatures.
template <class... CompletionSigs>
using AnySenderOf = typename exec::any_receiver_ref<stdexec::completion_signatures<CompletionSigs...>>::template any_sender<>;

//! Type-erased scheduler. The schedule sender from most real schedulers can
//! complete with either @c set_value or @c set_stopped, so both completions
//! are included.
using AnyScheduler = typename exec::any_receiver_ref<stdexec::completion_signatures<stdexec::set_value_t(), stdexec::set_stopped_t()>>::template any_sender<>::template any_scheduler<>;

}; // namespace execution
