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
		case NameDelimiterType::CXX_MODULE:
			return ":";
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
	if (s == nameDelimiterTypeToString(NameDelimiterType::CXX_MODULE))
	{
		return NameDelimiterType::CXX_MODULE;
	}
	return NameDelimiterType::UNKNOWN;
}

inline NameDelimiterType detectDelimiterType(const std::string& name)
{
	// CXX ("::") must be probed before CXX_MODULE (":") -- every "::" also contains ":".
	static const std::array allDelimiters{
		NameDelimiterType::FILE, NameDelimiterType::CXX, NameDelimiterType::CXX_MODULE};

	for (NameDelimiterType delimiter: allDelimiters)
	{
		if (name.find(nameDelimiterTypeToString(delimiter)) != std::string::npos)
		{
			return delimiter;
		}
	}

	return NameDelimiterType::UNKNOWN;
}
