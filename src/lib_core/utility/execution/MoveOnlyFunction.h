#pragma once

//-----------------------------------------------------------------------
/*!
 * @file	MoveOnlyFunction.h
 *
 * @brief	std::move_only_function, where the standard library has it.
 *
 * libc++ (LLVM 23 included) does not implement std::move_only_function yet,
 * while libstdc++ and MSVC's STL do. Elsewhere this is a minimal stand-in with
 * the calls it is used for: built from any callable, also a move-only one;
 * moved, not copied; empty when default-built or built from nullptr; and
 * called through a non-const operator().
 */
//-----------------------------------------------------------------------

#include <functional>

#if !defined(__cpp_lib_move_only_function)
#include <concepts>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#endif

namespace execution
{
#if defined(__cpp_lib_move_only_function)

template <class Signature>
using MoveOnlyFunction = std::move_only_function<Signature>;

#else

template <class Signature>
class MoveOnlyFunction;

template <class R, class... Args>
class MoveOnlyFunction<R(Args...)>
{
public:
	MoveOnlyFunction() noexcept = default;
	MoveOnlyFunction(std::nullptr_t) noexcept {}

	template <class F>
		requires(!std::same_as<std::remove_cvref_t<F>, MoveOnlyFunction> &&
				 std::is_invocable_r_v<R, std::decay_t<F>&, Args...>)
	MoveOnlyFunction(F&& f): m_callable(std::make_unique<Callable<std::decay_t<F>>>(std::forward<F>(f)))
	{
	}

	MoveOnlyFunction(MoveOnlyFunction&&) noexcept = default;
	MoveOnlyFunction& operator=(MoveOnlyFunction&&) noexcept = default;
	MoveOnlyFunction(const MoveOnlyFunction&) = delete;
	MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

	explicit operator bool() const noexcept
	{
		return m_callable != nullptr;
	}

	R operator()(Args... args)
	{
		return m_callable->call(std::forward<Args>(args)...);
	}

private:
	struct Base
	{
		virtual ~Base() = default;
		virtual R call(Args... args) = 0;
	};

	template <class F>
	struct Callable final: Base
	{
		template <class G>
		explicit Callable(G&& g): f(std::forward<G>(g))
		{
		}

		R call(Args... args) override
		{
			return std::invoke(f, std::forward<Args>(args)...);
		}

		F f;
	};

	std::unique_ptr<Base> m_callable;
};

#endif
}	 // namespace execution
