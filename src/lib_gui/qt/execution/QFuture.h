#pragma once

// Qt sender/receiver bridge. Originally by Ville Voutilainen (libunifex-qt),
// ported from libunifex to NVIDIA stdexec.
//-----------------------------------------------------------------------
/*!
 * @file	QFuture.h
 *
 * @brief	Bridges between @c QFuture<T> and stdexec senders.
 *
 * Two directions:
 *   - @c FromQFuture(fut) : adapt an existing @c QFuture<T> into a sender
 *                           that completes with @c set_value(T) (or
 *                           @c set_stopped on cancellation, or
 *                           @c set_error on exception).
 *   - @c ToQFuture(scope, sender) : spawn @p sender into @p scope and
 *                           return a @c QFuture<T> that resolves when the
 *                           sender completes. Exceptions are translated
 *                           via @c QException / @c QUnhandledException so
 *                           that @c QFuture::result() rethrows them on
 *                           the consumer thread.
 */
//-----------------------------------------------------------------------

#include "StdexecPrelude.h"

#include <QException>
#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QPromise>

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace execution::qt
{

namespace ex = stdexec;

namespace detail
{

template <class T>
struct QFutureSender
{
	using sender_concept		= ex::sender_t;
	using completion_signatures = ex::completion_signatures<ex::set_value_t(T), ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>;

	QFuture<T> fut;

	template <class R>
	struct Operation
	{
		R rcvr;
		QFuture<T> fut;
		std::unique_ptr<QFutureWatcher<T>> watcher;

		friend void tag_invoke(ex::start_t, Operation &op) noexcept
		{
			if(op.fut.isCanceled())
			{
				ex::set_stopped(std::move(op.rcvr));
				return;
			}
			if(op.fut.isFinished())
			{
				op.Complete();
				return;
			}
			op.watcher = std::make_unique<QFutureWatcher<T>>();
			QObject::connect(op.watcher.get(), &QFutureWatcher<T>::finished, op.watcher.get(), [&op]() noexcept { op.Complete(); });
			op.watcher->setFuture(op.fut);
		}

		void Complete() noexcept
		{
			if(fut.isCanceled())
			{
				ex::set_stopped(std::move(rcvr));
				return;
			}
			try
			{
				if constexpr(std::is_void_v<T>)
				{
					fut.waitForFinished();
					ex::set_value(std::move(rcvr));
				}
				else
				{
					ex::set_value(std::move(rcvr), fut.result());
				}
			}
			catch(...)
			{
				ex::set_error(std::move(rcvr), std::current_exception());
			}
		}
	};

	template <class R>
	friend Operation<std::decay_t<R>> tag_invoke(ex::connect_t, QFutureSender s, R &&r)
	{
		return { std::forward<R>(r), std::move(s.fut), nullptr };
	}
};

}; // namespace detail

//! Adapt an existing @c QFuture<T> into a stdexec sender.
template <class T>
[[nodiscard]] detail::QFutureSender<T> FromQFuture(QFuture<T> fut) noexcept
{
	return { std::move(fut) };
}

//! Spawn @p sender into @p scope and return a @c QFuture<T> resolving with
//! its result. Exceptions are translated to @c QException so they survive
//! the @c QFuture marshaling.
template <ex::sender S, class T>
[[nodiscard]] QFuture<T> ToQFuture(exec::async_scope &scope, S &&sender)
{
	auto promise = std::make_shared<QPromise<T>>();
	promise->start();
	auto fut = promise->future();

	scope.spawn(std::forward<S>(sender) |
				ex::then(
					[promise](T value)
					{
						if constexpr(!std::is_void_v<T>)
							promise->addResult(std::move(value));
						promise->finish();
					}) |
				ex::upon_error(
					[promise](std::exception_ptr e)
					{
						try
						{
							std::rethrow_exception(e);
						}
						catch(const QException &q)
						{
							promise->setException(q);
						}
						catch(...)
						{
							promise->setException(QUnhandledException{ e });
						}
						promise->finish();
					}) |
				ex::upon_stopped([promise]() { promise->finish(); }));
	return fut;
}

}; // namespace execution::qt
