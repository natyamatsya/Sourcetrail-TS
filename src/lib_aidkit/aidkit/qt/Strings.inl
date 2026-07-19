// Inline implementations for Strings.hpp. Included at the end of that header; not a standalone TU.

#pragma once

namespace aidkit::qt {

inline QChar operator ""_qc(char c)
{
	return QChar(c);
}

inline QString operator ""_qs(const char *str, std::size_t len)
{
	return QString::fromUtf8(str, static_cast<qsizetype>(len));
}

inline std::ostream &operator<<(std::ostream &output, const QString &qstring)
{
	return output << qstring.toStdString();
}

}
