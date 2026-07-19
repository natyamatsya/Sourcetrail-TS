#ifndef LOG_FACADE_H
#define LOG_FACADE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include "LogManager.h"

// Classic build only: also expose the legacy LOG_* macros. A converted header switches
// `#include "logging.h"` -> `#include "LogFacade.h"` for its own inline srctrl::log use; re-exposing the
// macros here keeps that a drop-in for the header's #include-based consumers (which relied on getting
// LOG_* transitively). In the module purview this is skipped -- macros don't cross `import`, and the
// module surface is srctrl::log.
#include "logging.h"
#endif

// Module-native logging front end (exported by `srctrl.logging`). It replaces the preprocessor capture
// the LOG_* macros do (`__FILE__`/`__FUNCTION__`/`__LINE__`) with std::source_location, so the whole
// surface is ordinary functions/templates and therefore exportable from a module -- no macro has to
// cross an `import`. The backend is unchanged: every call routes to the classic LogManager singleton.
// The 12 LOG_* macros in logging.h stay as the classic compat shim (same backend, independent front
// end), so existing call sites keep compiling untouched.
SRCTRL_EXPORT namespace srctrl::log
{
	// Severity ordering; `min_level` is the compile-time floor -- calls strictly below it are discarded
	// by `if constexpr` (arguments included), the function-based analog of `#define LOG_x(...) ((void)0)`.
	enum class level
	{
		info,
		warning,
		error
	};

	inline constexpr level min_level = level::info;

	// Folds a std::format string together with its call-site source_location into one non-variadic
	// leading argument, so the trailing pack still deduces normally. The wrapper's own pack is pinned
	// out of deduction at the call boundary with std::type_identity_t (see the *f overloads); the
	// consteval constructor keeps format-string checking at compile time.
	template <class... Args>
	struct fmt_with_loc
	{
		std::format_string<Args...> fmt;
		std::source_location loc;

		template <class S>
		consteval fmt_with_loc(
			const S& s, std::source_location l = std::source_location::current())
			: fmt{s}, loc{l}
		{
		}
	};

	// Plain (non-format) overloads -- brace-safe (no format parsing), take any string. These are the
	// closest match to today's LOG_x("literal") sites and what the macro shim conceptually forwards to.
	void info(std::string_view message, std::source_location loc = std::source_location::current());
	void warning(std::string_view message, std::source_location loc = std::source_location::current());
	void error(std::string_view message, std::source_location loc = std::source_location::current());

	// std::format overloads. The `f` suffix avoids an ambiguity: a bare string literal could match both
	// the string_view overload and fmt_with_loc's consteval converting constructor.
	template <class... Args>
	void infof(fmt_with_loc<std::type_identity_t<Args>...> f, Args&&... args);
	template <class... Args>
	void warningf(fmt_with_loc<std::type_identity_t<Args>...> f, Args&&... args);
	template <class... Args>
	void errorf(fmt_with_loc<std::type_identity_t<Args>...> f, Args&&... args);

	// Lazy overloads: the message factory is invoked only when the level is live -- the type-safe
	// equivalent of the LOG_x_STREAM macros' "gate before building the string" behavior, and the only
	// way to also skip evaluating genuinely expensive message arguments.
	template <class F>
	void info_lazy(F&& makeMessage, std::source_location loc = std::source_location::current());
	template <class F>
	void warning_lazy(F&& makeMessage, std::source_location loc = std::source_location::current());
	template <class F>
	void error_lazy(F&& makeMessage, std::source_location loc = std::source_location::current());
}

#include "LogFacade.inl"

#endif	  // LOG_FACADE_H
