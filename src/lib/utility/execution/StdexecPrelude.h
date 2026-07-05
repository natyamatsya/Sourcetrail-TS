#pragma once

//-----------------------------------------------------------------------
/*!
 * @file	StdexecPrelude.h
 *
 * @brief	Centralised, warning-silenced entry point for stdexec / exec
 *			headers.
 *
 * NVIDIA stdexec's @c __tuple.hpp emits @c C4702 (unreachable code) on
 * MSVC during back-end dataflow analysis of instantiated templates. That
 * class of warning is not suppressed by MSVC's @c /external:W0 shield,
 * because it fires after template instantiation when the originating
 * header's "external" provenance has already been lost. To keep our
 * build clean without pinning a project-wide @c /wd4702, every TU that
 * needs stdexec routes its include through this header instead of
 * including @c <stdexec/...> or @c <exec/...> directly.
 *
 * Add new stdexec / exec headers here as the codebase grows; never
 * include them outside this file.
 */
//-----------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702) // unreachable code in stdexec/__detail/__tuple.hpp
#pragma warning(disable : 4714) // function 'function' marked as __forceinline not inlined (can be tuned later)
#endif

#include <exec/any_sender_of.hpp>
#include <exec/async_scope.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp> // provides stdexec::run_loop

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
