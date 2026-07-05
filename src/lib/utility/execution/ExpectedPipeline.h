#pragma once

//-----------------------------------------------------------------------
/*!
 * @file	ExpectedPipeline.h
 *
 * @brief	Combinators that compose @c std::expected -returning transformations
 *			inside a stdexec sender pipeline.
 *
 * Convention (see ADR-0001): domain errors flow through the sender's
 * value channel as @c std::expected<T, E>. The error channel
 * (@c set_error_t(std::exception_ptr)) is reserved for exceptional
 * conditions, and cancellation flows through @c set_stopped.
 *
 * Provided combinators:
 *   - @c ThenOk(f)             : lift a plain transform onto the success path.
 *   - @c AndThenOk(f)          : monadic bind; @c f returns @c std::expected.
 *   - @c OrElseOk(f)           : recovery from an error.
 *   - @c TransformErrorOk(f)   : map the error type at service boundaries.
 */
//-----------------------------------------------------------------------

#include "StdexecPrelude.h"

#include <concepts>
#include <expected>
#include <functional>
#include <type_traits>
#include <utility>

namespace execution
{

// --- Expected concept and helpers ----------------------------------------

namespace detail
{

template <class>
struct ExpectedTraits : std::false_type
{
};

template <class T, class E>
struct ExpectedTraits<std::expected<T, E>> : std::true_type
{
	using ValueType = T;
	using ErrorType = E;
};

}; // namespace detail

//! Concept: @c T is some specialization of @c std::expected.
template <class T>
concept Expected = detail::ExpectedTraits<std::remove_cvref_t<T>>::value;

template <Expected T>
using ExpectedValueT = typename detail::ExpectedTraits<std::remove_cvref_t<T>>::ValueType;

template <Expected T>
using ExpectedErrorT = typename detail::ExpectedTraits<std::remove_cvref_t<T>>::ErrorType;

//! Concept: @c F invoked with @c V&& returns some @c std::expected specialization.
template <class F, class V>
concept ExpectedReturning = std::invocable<F &, V &&> && Expected<std::invoke_result_t<F &, V &&>>;

// --- Combinators ---------------------------------------------------------

//! Lift a plain transform onto the success path. If the incoming
//! @c std::expected is an error, it is forwarded unchanged.
template <class F>
[[nodiscard]] auto ThenOk(F f)
{
	return stdexec::then(
		[f = std::move(f)]<Expected Exp>(Exp exp)
		{
			using V = ExpectedValueT<Exp>;
			using E = ExpectedErrorT<Exp>;
			using U = std::invoke_result_t<F &, V &&>;
			if(!exp)
				return std::expected<U, E>{ std::unexpect, std::move(exp).error() };
			return std::expected<U, E>{ std::invoke(f, std::move(*exp)) };
		});
}

//! Monadic bind: @c f returns another @c std::expected, which becomes the
//! pipeline's next value.
template <class F>
[[nodiscard]] auto AndThenOk(F f)
{
	return stdexec::then(
		[f = std::move(f)]<Expected Exp>(Exp exp)
			requires ExpectedReturning<F, ExpectedValueT<Exp>>
		{
			using V = ExpectedValueT<Exp>;
			using R = std::invoke_result_t<F &, V &&>;
			if(!exp)
				return R{ std::unexpect, std::move(exp).error() };
			return std::invoke(f, std::move(*exp));
		});
}

//! Recovery: if the incoming @c std::expected is an error, @c f is invoked
//! with the error and its return value becomes the new value.
template <class F>
[[nodiscard]] auto OrElseOk(F f)
{
	return stdexec::then(
		[f = std::move(f)]<Expected Exp>(Exp exp)
		{
			using R = std::invoke_result_t<F &, ExpectedErrorT<Exp> &&>;
			if(exp)
				return R{ std::move(*exp) };
			return std::invoke(f, std::move(exp).error());
		});
}

//! Map the error type at service boundaries. Successful values pass through
//! unchanged.
template <class F>
[[nodiscard]] auto TransformErrorOk(F f)
{
	return stdexec::then(
		[f = std::move(f)]<Expected Exp>(Exp exp)
		{
			using V	 = ExpectedValueT<Exp>;
			using E2 = std::invoke_result_t<F &, ExpectedErrorT<Exp> &&>;
			if(exp)
				return std::expected<V, E2>{ std::move(*exp) };
			return std::expected<V, E2>{ std::unexpect, std::invoke(f, std::move(exp).error()) };
		});
}

}; // namespace execution
