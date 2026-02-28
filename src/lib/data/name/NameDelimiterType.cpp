#include "NameDelimiterType.h"

#include <array>

static const std::array allDelimiters
{
	NameDelimiterType::FILE,
	NameDelimiterType::CXX
};

std::string nameDelimiterTypeToString(NameDelimiterType delimiter)
{
	switch (delimiter)
	{
		case NameDelimiterType::FILE:
			return "/";
		case NameDelimiterType::CXX:
			return "::";
		default:
			break;
	}
	return "@";
}

NameDelimiterType stringToNameDelimiterType(const std::string &s)
{
	if (s == nameDelimiterTypeToString(NameDelimiterType::FILE))
	{
		return NameDelimiterType::FILE;
	}
	if (s == nameDelimiterTypeToString(NameDelimiterType::CXX))
	{
		return NameDelimiterType::CXX;
	}
	return NameDelimiterType::UNKNOWN;
}

NameDelimiterType detectDelimiterType(const std::string &name)
{
	for (NameDelimiterType delimiter : allDelimiters)
	{
		if (name.find(nameDelimiterTypeToString(delimiter)) != std::string::npos)
		{
			return delimiter;
		}
	}

	return NameDelimiterType::UNKNOWN;
}
