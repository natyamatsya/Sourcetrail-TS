#ifndef UTILITY_EXPECTED_H
#define UTILITY_EXPECTED_H

#include <expected>
#include <exception>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

#if defined(__GNUG__)
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace utility
{
template <typename TCode>
struct ExpectedError
{
	TCode code;
	std::string message;
	std::string exceptionType;
};

inline std::string getExceptionTypeName(const std::exception& exception)
{
#if defined(__GNUG__)
	int status{0};
	std::unique_ptr<char, decltype(&std::free)> demangledName{
		abi::__cxa_demangle(typeid(exception).name(), nullptr, nullptr, &status), &std::free};
	if (status == 0 && demangledName)
		return demangledName.get();
#endif

	return typeid(exception).name();
}

template <typename TCode>
ExpectedError<TCode> makeExpectedError(
	const TCode code,
	const std::string& message,
	const std::string& exceptionType = "")
{
	return {code, message, exceptionType};
}

template <typename TCode>
std::string expectedErrorToString(const ExpectedError<TCode>& error)
{
	if (error.exceptionType.empty())
		return error.message;

	return error.message + " [" + error.exceptionType + "]";
}

template <typename TCode>
std::ostream& operator<<(std::ostream& stream, const ExpectedError<TCode>& error)
{
	stream << expectedErrorToString(error);
	return stream;
}

template <typename TResult, typename TCode, typename TCallable>
std::expected<TResult, ExpectedError<TCode>> expectedFromExceptions(
	const TCode exceptionCode,
	const TCode unknownExceptionCode,
	const std::string& context,
	TCallable&& callable)
{
	try
	{
		if constexpr (std::is_void_v<TResult>)
		{
			std::forward<TCallable>(callable)();
			return {};
		}
		else
		{
			return std::forward<TCallable>(callable)();
		}
	}
	catch (const std::exception& exception)
	{
		return std::unexpected(makeExpectedError(
			exceptionCode,
			context + ": " + std::string(exception.what()),
			getExceptionTypeName(exception)));
	}
	catch (...)
	{
		return std::unexpected(makeExpectedError(
			unknownExceptionCode, context, "unknown exception type"));
	}
}
} // namespace utility

#endif // UTILITY_EXPECTED_H
