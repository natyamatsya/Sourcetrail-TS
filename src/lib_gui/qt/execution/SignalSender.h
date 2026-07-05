#pragma once

// Qt sender/receiver bridge. Originally by Ville Voutilainen (libunifex-qt),
// ported from libunifex to NVIDIA stdexec.
//-----------------------------------------------------------------------
/*!
 * @file	SignalSender.h
 *
 * @brief	Turn the next emission of a QObject signal into a stdexec sender
 *			that completes with @c set_value(std::tuple<Args...>).
 *
 * Mirrors Voutilainen's libunifex-with-Qt approach: a one-shot adapter
 * suitable for pipelines that wait for a specific event. The connection
 * is torn down on completion, cancellation, or destruction of the
 * source @c QObject.
 *
 * Use via the @c FromSignal factory:
 *   @code
 *   FromSignal(button, &QPushButton::clicked)
 *       | stdexec::then([] (auto) { ... })
 *   @endcode
 */
//-----------------------------------------------------------------------

#include "Concepts.h"
#include "StdexecPrelude.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <Qt>

#include <atomic>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace execution::qt
{

namespace ex = stdexec;

//-----------------------------------------------------------------------
/*!
 * @class	SignalSender
 *
 * @brief	One-shot stdexec sender that fires on the next emission of a
 *			given @c QObject signal.
 */
//-----------------------------------------------------------------------
template <QObjectDerived Class, class... Args>
class SignalSender
{
public:
	using sender_concept		= ex::sender_t;
	using ValueTuple			= std::tuple<std::decay_t<Args>...>;
	using completion_signatures = ex::completion_signatures<ex::set_value_t(ValueTuple), ex::set_stopped_t()>;

	using SignalPtr = void (Class::*)(Args...);

	SignalSender(Class *obj, SignalPtr signal) noexcept
		: m_obj{ obj }
		, m_signal{ signal }
	{
	}

	template <class R>
	struct Operation
	{
		R rcvr;
		QPointer<Class> obj;
		SignalPtr signal;
		QMetaObject::Connection conn;
		QMetaObject::Connection destroyedConn;
		std::atomic<bool> completed{ false };

		struct OnStop
		{
			Operation *self;
			void operator()() noexcept
			{
				if(self->completed.exchange(true))
					return;
				QObject::disconnect(self->conn);
				QObject::disconnect(self->destroyedConn);
				ex::set_stopped(std::move(self->rcvr));
			}
		};
		using TokenT	= ex::stop_token_of_t<ex::env_of_t<R>>;
		using CallbackT = typename TokenT::template callback_type<OnStop>;
		std::optional<CallbackT> stopCb;

		void start() & noexcept
		{
			if(!obj)
			{
				ex::set_stopped(std::move(rcvr));
				return;
			}

			conn = QObject::connect(
				obj.data(), signal, obj.data(),
				[this](std::decay_t<Args>... args)
				{
					if(completed.exchange(true))
						return;
					QObject::disconnect(conn);
					QObject::disconnect(destroyedConn);
					stopCb.reset();
					ex::set_value(std::move(rcvr), ValueTuple{ std::move(args)... });
				},
				::Qt::DirectConnection);

			destroyedConn = QObject::connect(
				obj.data(), &QObject::destroyed, obj.data(),
				[this]()
				{
					if(completed.exchange(true))
						return;
					QObject::disconnect(conn);
					stopCb.reset();
					ex::set_stopped(std::move(rcvr));
				},
				::Qt::DirectConnection);

			stopCb.emplace(ex::get_stop_token(ex::get_env(rcvr)), OnStop{ this });
		}
	};

	template <class R>
	Operation<std::decay_t<R>> connect(R &&r) const noexcept
	{
		return { std::forward<R>(r), m_obj, m_signal, {}, {}, {}, std::nullopt };
	}

private:
	QPointer<Class> m_obj;
	SignalPtr m_signal;
};

//! Factory: deduces argument types from the member function pointer.
template <QObjectDerived Class, class... Args>
[[nodiscard]] SignalSender<Class, Args...> FromSignal(Class *obj, void (Class::*signal)(Args...)) noexcept
{
	return { obj, signal };
}

static_assert(ex::sender<SignalSender<QObject>>, "SignalSender must model stdexec::sender");

}; // namespace execution::qt
