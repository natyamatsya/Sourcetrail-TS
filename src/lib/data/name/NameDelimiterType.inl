// Inline implementations for NameDelimiterType.h (included at the end of that header).

#pragma once

inline std::string nameDelimiterTypeToString(NameDelimiterType delimiter)
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

inline NameDelimiterType stringToNameDelimiterType(const std::string& s)
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

inline NameDelimiterType detectDelimiterType(const std::string& name)
{
	static const std::array allDelimiters{NameDelimiterType::FILE, NameDelimiterType::CXX};

	for (NameDelimiterType delimiter: allDelimiters)
	{
		if (name.find(nameDelimiterTypeToString(delimiter)) != std::string::npos)
		{
			return delimiter;
		}
	}

	return NameDelimiterType::UNKNOWN;
}
