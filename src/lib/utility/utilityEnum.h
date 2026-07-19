#ifndef UTILITY_ENUM_H
#define UTILITY_ENUM_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <limits>
#include <ostream>
#include <string>
#include <type_traits>
#endif

SRCTRL_EXPORT template <typename E>
concept EnumType = std::is_enum_v<E>;

SRCTRL_EXPORT template <EnumType E>
constexpr auto MAX_ENUM_VALUE = std::numeric_limits<std::underlying_type_t<E>>::max();

SRCTRL_EXPORT template <EnumType E>
E intToEnum(int);

SRCTRL_EXPORT template <EnumType E, std::size_t N>
E lookupEnum(int i, const E (&enums)[N], E &&defaultValue)
{
	if (i >= 0 && static_cast<std::size_t>(i) < N)
		return enums[i];
	else
		return defaultValue;
}

SRCTRL_EXPORT template <EnumType E>
constexpr std::underlying_type_t<E> underlying_type_cast(E e)
{
	return static_cast<std::underlying_type_t<E>>(e);
}

SRCTRL_EXPORT template <EnumType E>
std::string to_string(E e)
{
	return std::to_string(underlying_type_cast(e));
}

SRCTRL_EXPORT template <EnumType E>
std::ostream &operator << (std::ostream &stream, E e)
{
	return stream << underlying_type_cast(e);
}

// Support for enum bit operations:

SRCTRL_EXPORT template <typename E>
concept EnumFlagType = EnumType<E> && std::is_unsigned_v<std::underlying_type_t<E>>;

SRCTRL_EXPORT template <EnumFlagType E>
constexpr E operator | (E e1, E e2)
{
	return static_cast<E>(underlying_type_cast(e1) | underlying_type_cast(e2));
}

SRCTRL_EXPORT template <EnumFlagType E>
constexpr E operator & (E e1, E e2)
{
	return static_cast<E>(underlying_type_cast(e1) & underlying_type_cast(e2));
}

#endif
