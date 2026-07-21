#ifndef UTILITY_STRING_H
#define UTILITY_STRING_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <deque>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#endif

namespace utility
{


SRCTRL_EXPORT std::deque<std::string> split(const std::string& str, char delimiter);
SRCTRL_EXPORT std::deque<std::string> split(const std::string& str, const std::string& delimiter);
SRCTRL_EXPORT std::vector<std::string> splitToVector(const std::string& str, char delimiter);
SRCTRL_EXPORT std::vector<std::string> splitToVector(const std::string& str, const std::string& delimiter);

SRCTRL_EXPORT std::string join(const std::deque<std::string>& list, char delimiter);
SRCTRL_EXPORT std::string join(const std::deque<std::string>& list, const std::string& delimiter);
SRCTRL_EXPORT std::string join(const std::vector<std::string>& list, char delimiter);
SRCTRL_EXPORT std::string join(const std::vector<std::string>& list, const std::string& delimiter);

SRCTRL_EXPORT std::deque<std::string> tokenize(const std::string& str, char delimiter);
SRCTRL_EXPORT std::deque<std::string> tokenize(const std::string& str, const std::string& delimiter);
SRCTRL_EXPORT std::deque<std::string> tokenize(const std::deque<std::string>& list, char delimiter);
SRCTRL_EXPORT std::deque<std::string> tokenize(const std::deque<std::string>& list, const std::string& delimiter);

SRCTRL_EXPORT std::string substrBeforeFirst(const std::string& str, char delimiter);
SRCTRL_EXPORT std::string substrBeforeFirst(const std::string& str, const std::string& delimiter);
SRCTRL_EXPORT std::string substrBeforeLast(const std::string& str, char delimiter);
SRCTRL_EXPORT std::string substrAfterLast(const std::string& str, char delimiter);
SRCTRL_EXPORT std::string substrAfter(const std::string& str, char delimiter);
SRCTRL_EXPORT std::string substrAfter(const std::string& str, const std::string& delimiter);
SRCTRL_EXPORT std::string substrBetween(const std::string& str, const std::string& delimiter1, const std::string& delimiter2);

SRCTRL_EXPORT bool isPrefix(const std::string_view prefix, const std::string_view text);
SRCTRL_EXPORT bool isPostfix(const std::string_view postfix, const std::string_view text);

SRCTRL_EXPORT std::string replace(std::string str, const std::string& from, const std::string& to);
SRCTRL_EXPORT std::string replaceBetween(const std::string& str, char startDelimiter, char endDelimiter, const std::string& to);

SRCTRL_EXPORT std::string insertLineBreaksAtBlankSpaces(const std::string& s, std::size_t maxLineLength);
SRCTRL_EXPORT std::string breakSignature(std::string signature, std::size_t maxLineLength, std::size_t tabWidth);
SRCTRL_EXPORT std::string breakSignature(std::string returnPart, std::string namePart, std::string paramPart, std::size_t maxLineLength, std::size_t tabWidth);

SRCTRL_EXPORT std::string trim(const std::string& str);

SRCTRL_EXPORT enum class ElideMode
{
	ELIDE_LEFT,
	ELIDE_MIDDLE,
	ELIDE_RIGHT
};

SRCTRL_EXPORT std::string elide(const std::string& str, ElideMode mode, std::size_t size);

SRCTRL_EXPORT std::string convertWhiteSpacesToSingleSpaces(const std::string& str);


SRCTRL_EXPORT template <typename ContainerType>
ContainerType split(const std::string& str, const std::string& delimiter)
{
	std::size_t pos = 0;
	std::size_t oldPos = 0;
	ContainerType c;

	do
	{
		pos = str.find(delimiter, oldPos);
		c.push_back(str.substr(oldPos, pos - oldPos));
		oldPos = pos + delimiter.size();
	} while (pos < str.length());

	return c;
}

SRCTRL_EXPORT template <typename ContainerType>
std::string join(const ContainerType& list, const std::string& delimiter)
{
	std::stringstream ss;
	bool first = true;
	for (const std::string& str: list)
	{
		if (!first)
		{
			ss << delimiter;
		}
		first = false;

		ss << str;
	}
	return ss.str();
}

//
// Locale specific functions:
//

SRCTRL_EXPORT std::u32string convertToUtf32(const std::string &utf8chars);

SRCTRL_EXPORT std::string toLowerCase(const std::string& in);

SRCTRL_EXPORT bool isCaseInsensitiveEqual(const std::string& a, const std::string& b);

SRCTRL_EXPORT bool isCaseInsensitiveLess(const std::string& s1, const std::string& s2);

}	 // namespace utility

#include "utilityString.inl"

#endif	  // UTILITY_STRING_H
