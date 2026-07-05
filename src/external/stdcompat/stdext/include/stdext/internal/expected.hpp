#pragma once

// -----------------------------------------------------------------------------
// stdext/expected.hpp  -- NON-STANDARD reference extension of std::expected
//
// stdext::expected<T, E> is a superset of std::expected that additionally
// accepts an lvalue reference value type, i.e. stdext::expected<T&, E> and
// stdext::expected<const T&, E>. std::expected itself rejects reference value
// types, and there is no standard expected<T&> (P2988 added optional<T&> only)
// -- so this is a deliberate, non-standard extension and lives under stdext::.
//
// For a non-reference T, stdext::expected<T, E> IS std::expected<T, E>. For a
// reference it is a thin adaptor over std::expected<reference_wrapper<T>, E>,
// with binding / assignment semantics modeled on std::optional<T&> (P2988).
//
// Requires C++23 (std::expected).
// -----------------------------------------------------------------------------

#include <expected>
#include <functional> // std::reference_wrapper, std::ref
#include <memory>	  // std::addressof
#include <type_traits>
#include <utility>

namespace stdext
{
namespace detail
{
// -----------------------------------------------------------------------------
// reference_expected<T, E> -- the storage type behind stdext::expected<T&, E>
// and stdext::expected<const T&, E>.
//
// Backed by std::expected<reference_wrapper<T>, E>. Holding the value as a
// reference_wrapper gives us, for free, the semantics P2988 chose for
// std::optional<T&>:
//   * assignment REBINDS the reference (it never assigns through), and
//   * binding from an rvalue / temporary is ill-formed (reference_wrapper deletes
//     its rvalue constructor), so no dangling reference can be formed.
// A reference to const is spelled with T = const U, so const-ness rides on T;
// derived-to-base binding works because reference_wrapper<Base> binds a Derived
// lvalue. The value is re-exposed as a bare T& through operator*/->/value();
// because the referent is never owned, the constness of *this does not propagate
// to it, matching std::reference_wrapper.
// -----------------------------------------------------------------------------
template <class T, class E>
class reference_expected
{
	using storage = std::expected<std::reference_wrapper<T>, E>;
	storage m_storage;

public:
	using value_type = T &;
	using error_type = E;

	// Success or error in one forwarding constructor: storage is constructible
	// from T& (binds the reference) and from unexpected<...> (the error channel),
	// while binding from an rvalue is rejected by reference_wrapper -- exactly the
	// P2988 constraint against dangling.
	template <class Arg, class = std::enable_if_t<!std::is_same<std::decay_t<Arg>, reference_expected>::value && std::is_constructible<storage, Arg &&>::value>>
	constexpr reference_expected(Arg &&arg) noexcept(std::is_nothrow_constructible<storage, Arg &&>::value)
		: m_storage(std::forward<Arg>(arg))
	{
	}

	reference_expected(const reference_expected &)			  = default;
	reference_expected(reference_expected &&)				  = default;
	reference_expected &operator=(const reference_expected &) = default;
	reference_expected &operator=(reference_expected &&)	  = default;

	// Rebind to another referent (reference semantics, like optional<T&>).
	constexpr reference_expected &operator=(T &ref) noexcept
	{
		m_storage = std::ref(ref);
		return *this;
	}

	[[nodiscard]] constexpr bool has_value() const noexcept { return m_storage.has_value(); }
	constexpr explicit operator bool() const noexcept { return m_storage.has_value(); }

	constexpr T &operator*() const noexcept { return m_storage->get(); }
	constexpr T *operator->() const noexcept { return std::addressof(m_storage->get()); }
	constexpr T &value() const { return m_storage.value().get(); }

	constexpr const E &error() const & noexcept { return m_storage.error(); }
	constexpr E &error() & noexcept { return m_storage.error(); }
	constexpr E &&error() && noexcept { return std::move(m_storage).error(); }
};

// Select: expected<T&, E> / expected<const T&, E> -> reference_expected; any
// non-reference T -> std::expected unchanged.
template <class T, class E>
struct expected_select
{
	using type = std::expected<T, E>;
};
template <class U, class E>
struct expected_select<U &, E>
{
	using type = reference_expected<U, E>;
};
} // namespace detail

// expected<T, E>: like std::expected, but T may also be an lvalue reference
// (T& or const T&), modeled on std::optional<T&> from P2988.
template <class T, class E>
using expected = typename detail::expected_select<T, E>::type;
} // namespace stdext
