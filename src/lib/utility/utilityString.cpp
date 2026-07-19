#include "utilityString.h"

#include <string>

#include <QString>

namespace utility
{

///////////////////////////////////////////////////////////////////////////////
//
// Locale specific functions:
//
///////////////////////////////////////////////////////////////////////////////

std::string toLowerCase(const std::string& in)
{
    return QString::fromStdString(in).toLower().toStdString();
}

bool isCaseInsensitiveEqual(const std::string &a, const std::string &b)
{
	return toLowerCase(a) == toLowerCase(b);
}

bool isCaseInsensitiveLess(const std::string& s1, const std::string& s2)
{
	return toLowerCase(s1) < toLowerCase(s2);
}

std::u32string convertToUtf32(const std::string &utf8chars)
{
	QString qs = QString::fromUtf8(utf8chars.data(), static_cast<int>(utf8chars.size()));
	std::u32string result;
	result.reserve(static_cast<size_t>(qs.size()));
	for (auto it = qs.cbegin(); it != qs.cend(); )
	{
		char32_t cp = it->unicode();
		if (it->isHighSurrogate() && (it + 1) != qs.cend() && (it + 1)->isLowSurrogate())
		{
			cp = QChar::surrogateToUcs4(it->unicode(), (it + 1)->unicode());
			++it;
		}
		result.push_back(cp);
		++it;
	}
	return result;
}

} // namespace utility
