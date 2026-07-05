#pragma once

// Qt sender/receiver bridge. Originally by Ville Voutilainen (libunifex-qt),
// ported from libunifex to NVIDIA stdexec.
//-----------------------------------------------------------------------
/*!
 * @file	QtThreadScheduler.h
 *
 * @brief	A stdexec scheduler that posts work to the thread on which a
 *			chosen @c QObject lives.
 *
 * Use this to marshal sender-pipeline completions onto a specific Qt
 * thread &mdash; most commonly the main (GUI) thread, via the
 * @c UiScheduler() convenience anchored to @c QCoreApplication.
 *
 * The scheduler holds the anchor as a @c QPointer, so if the anchor is
 * destroyed before the scheduled work runs, the operation completes via
 * @c set_stopped rather than running on a dangling object.
 */
//-----------------------------------------------------------------------

#include "SenderAliases.h"
#include "StdexecPrelude.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <Qt>

#include <utility>

namespace execution::qt
{

namespace ex = stdexec;

//-----------------------------------------------------------------------
/*!
 * @class	QtThreadScheduler
 *
 * @brief	stdexec scheduler that posts to a chosen @c QObject's thread.
 */
//-----------------------------------------------------------------------
class QtThreadScheduler
{
public:
	struct Sender
	{
		using sender_concept		= ex::sender_t;
		using completion_signatures = ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>;

		QPointer<QObject> anchor;

		//! Sender env: advertises the completion scheduler, which is what
		//! @c stdexec::scheduler checks for.
		struct Env
		{
			QPointer<QObject> anchor;

			QtThreadScheduler query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept { return QtThreadScheduler{ anchor.data() }; }
			QtThreadScheduler query(ex::get_completion_scheduler_t<ex::set_stopped_t>) const noexcept { return QtThreadScheduler{ anchor.data() }; }
		};

		[[nodiscard]] Env get_env() const noexcept { return { anchor }; }

		template <class R>
		struct Operation
		{
			R rcvr;
			QPointer<QObject> anchor;

			void start() & noexcept
			{
				if(!anchor)
				{
					ex::set_stopped(std::move(rcvr));
					return;
				}
				QMetaObject::invokeMethod(
					anchor.data(),
					[this]() noexcept
					{
						auto tok = ex::get_stop_token(ex::get_env(rcvr));
						if(tok.stop_requested())
							ex::set_stopped(std::move(rcvr));
						else
							ex::set_value(std::move(rcvr));
					},
					::Qt::QueuedConnection);
			}
		};

		template <class R>
		Operation<std::decay_t<R>> connect(R &&r) const noexcept
		{
			return { std::forward<R>(r), anchor };
		}
	};

	explicit QtThreadScheduler(QObject *anchor) noexcept
		: m_anchor{ anchor }
	{
	}

	[[nodiscard]] Sender schedule() const noexcept { return { QPointer<QObject>{ m_anchor } }; }

	[[nodiscard]] friend bool operator==(const QtThreadScheduler &, const QtThreadScheduler &) noexcept = default;

private:
	QObject *m_anchor;
};

//! Convenience: scheduler anchored at the @c QCoreApplication instance, i.e.
//! the GUI/main thread.
[[nodiscard]] inline QtThreadScheduler UiScheduler() noexcept
{
	return QtThreadScheduler{ QCoreApplication::instance() };
}

static_assert(ex::scheduler<QtThreadScheduler>, "QtThreadScheduler must model stdexec::scheduler");

}; // namespace execution::qt
