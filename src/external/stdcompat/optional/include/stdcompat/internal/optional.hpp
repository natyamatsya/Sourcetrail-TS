#pragma once

// -----------------------------------------------------------------------------
// stdcompat/optional.hpp
//
// Provides stdcompat::optional, including the optional<T&> (reference)
// specialization from P2988 ("std::optional<T&>", C++26).
//
// * If the toolchain has a conforming <optional> that already implements P2988
//   (__cpp_lib_optional >= 202506L) we simply alias the standard types.
// * Otherwise we fall back to beman::optional — the Beman Project's reference
//   implementation of P2988R12 — and expose it under the stdcompat:: names.
//
// We use beman::optional rather than tl::optional precisely because it tracks
// the standard wording: its semantics (notably assignment / rebinding of
// optional<T&>) match what std::optional<T&> will do, so flipping to the real
// std type later is a no-op for callers.
//
// Requires C++20 or newer (beman::optional uses concepts and <ranges>).
// -----------------------------------------------------------------------------

#if __cplusplus < 202002L
#error "stdcompat/optional.hpp needs at least C++20"
#endif

// ---------- Detect a P2988-capable <optional> --------------------------------
#if defined(__has_include)
#if __has_include(<optional>)
#define STDCOMPAT_HAS_STD_OPTIONAL 1
#include <version> // for feature-test macros
#else
#define STDCOMPAT_HAS_STD_OPTIONAL 0
#endif
#else
#define STDCOMPAT_HAS_STD_OPTIONAL 0
#endif

// __cpp_lib_optional was bumped to 202506L by P2988R12 (optional<T&>).
// The previous (C++23) value was 202110L, so this threshold cannot misfire on
// a standard library that lacks the reference specialization.
#if STDCOMPAT_HAS_STD_OPTIONAL && defined(__cpp_lib_optional) && __cpp_lib_optional >= 202506L

// ======  Standard implementation is available  ===============================
#include <optional>

namespace stdcompat
{
template <class T>
using optional = std::optional<T>;

using std::bad_optional_access;
using std::in_place;
using std::in_place_t;
using std::make_optional;
using std::nullopt;
using std::nullopt_t;
} // namespace stdcompat

#else

// ======  Fallback to beman::optional  ========================================
#include <beman/optional/optional.hpp>

namespace stdcompat
{
template <class T>
using optional = ::beman::optional::optional<T>;

using ::beman::optional::bad_optional_access;
using ::beman::optional::in_place;
using ::beman::optional::in_place_t;
using ::beman::optional::make_optional;
using ::beman::optional::nullopt;
using ::beman::optional::nullopt_t;
} // namespace stdcompat

#endif // fallback / standard switch
