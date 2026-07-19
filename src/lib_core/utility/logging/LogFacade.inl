// Inline implementations for LogFacade.h (included at the end of that header). All definitions are
// inline: an out-of-line member/function of an exported module would not resolve for importers, and
// these must also work as ordinary inline functions in the classic (non-module) build.

#pragma once

namespace srctrl::log
{
	namespace detail
	{
		// The single routing point onto the classic LogManager backend. source_location supplies the
		// file/function/line the LOG_* macros used to capture with the preprocessor (plus column, which
		// we currently drop -- LogManager takes no column).
		inline void dispatch(level lvl, const std::string& message, const std::source_location& loc)
		{
			const auto instance = LogManager::getInstance();
			switch (lvl)
			{
			case level::info:
				instance->logInfo(message, loc.file_name(), loc.function_name(), loc.line());
				break;
			case level::warning:
				instance->logWarning(message, loc.file_name(), loc.function_name(), loc.line());
				break;
			case level::error:
				instance->logError(message, loc.file_name(), loc.function_name(), loc.line());
				break;
			}
		}
	}

	inline void info(std::string_view message, std::source_location loc)
	{
		if constexpr (level::info >= min_level)
		{
			if (LogManager::getInstance()->getLoggingEnabled())
			{
				detail::dispatch(level::info, std::string(message), loc);
			}
		}
	}

	inline void warning(std::string_view message, std::source_location loc)
	{
		if constexpr (level::warning >= min_level)
		{
			if (LogManager::getInstance()->getLoggingEnabled())
			{
				detail::dispatch(level::warning, std::string(message), loc);
			}
		}
	}

	inline void error(std::string_view message, std::source_location loc)
	{
		if constexpr (level::error >= min_level)
		{
			if (LogManager::getInstance()->getLoggingEnabled())
			{
				detail::dispatch(level::error, std::string(message), loc);
			}
		}
	}

	template <class... Args>
	void infof(fmt_with_loc<std::type_identity_t<Args>...> f, Args&&... args)
	{
		if constexpr (level::info >= min_level)
		{
			if (LogManager::getInstance()->getLoggingEnabled())
			{
				detail::dispatch(level::info, std::format(f.fmt, std::forward<Args>(args)...), f.loc);
			}
		}
	}

	template <class... Args>
	void warningf(fmt_with_loc<std::type_identity_t<Args>...> f, Args&&... args)
	{
		if constexpr (level::warning >= min_level)
		{
			if (LogManager::getInstance()->getLoggingEnabled())
			{
				detail::dispatch(
					level::warning, std::format(f.fmt, std::forward<Args>(args)...), f.loc);
			}
		}
	}

	template <class... Args>
	void errorf(fmt_with_loc<std::type_identity_t<Args>...> f, Args&&... args)
	{
		if constexpr (level::error >= min_level)
		{
			if (LogManager::getInstance()->getLoggingEnabled())
			{
				detail::dispatch(level::error, std::format(f.fmt, std::forward<Args>(args)...), f.loc);
			}
		}
	}

	template <class F>
	void info_lazy(F&& makeMessage, std::source_location loc)
	{
		if constexpr (level::info >= min_level)
		{
			if (LogManager::getInstance()->getLoggingEnabled())
			{
				detail::dispatch(level::info, std::forward<F>(makeMessage)(), loc);
			}
		}
	}

	template <class F>
	void warning_lazy(F&& makeMessage, std::source_location loc)
	{
		if constexpr (level::warning >= min_level)
		{
			if (LogManager::getInstance()->getLoggingEnabled())
			{
				detail::dispatch(level::warning, std::forward<F>(makeMessage)(), loc);
			}
		}
	}

	template <class F>
	void error_lazy(F&& makeMessage, std::source_location loc)
	{
		if constexpr (level::error >= min_level)
		{
			if (LogManager::getInstance()->getLoggingEnabled())
			{
				detail::dispatch(level::error, std::forward<F>(makeMessage)(), loc);
			}
		}
	}
}
