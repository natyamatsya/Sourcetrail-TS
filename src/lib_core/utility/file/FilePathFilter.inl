// Inline implementations for FilePathFilter.h. Included at the end of that header; not a
// standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <QString>
#endif

inline FilePathFilter::FilePathFilter(const std::string& filterString)
	: m_filterString(filterString), m_filterRegex(convertFilterStringToRegex(filterString))
{
}

inline std::string FilePathFilter::str() const
{
	return m_filterString;
}

inline bool FilePathFilter::isMatching(const FilePath& filePath) const
{
	return isMatching(filePath.str());
}

inline bool FilePathFilter::isMatching(const std::string& fileStr) const
{
	return m_filterRegex.match(QString::fromStdString(fileStr)).hasMatch();
}

inline bool FilePathFilter::operator<(const FilePathFilter& other) const
{
	return m_filterString.compare(other.m_filterString) < 0;
}

inline QRegularExpression FilePathFilter::convertFilterStringToRegex(const std::string& filterString)
{
	QString filter = QString::fromStdString(filterString);

	// Normalize path separators to forward slash
	filter.replace('\\', '/');

	// Build the regex pattern character by character
	QString pattern;
	int i = 0;
	while (i < filter.size())
	{
		QChar c = filter[i];
		if (c == '*')
		{
			if (i + 1 < filter.size() && filter[i + 1] == '*')
			{
				pattern += QString(".*");
				i += 2;
			}
			else
			{
				pattern += QString("[^\\\\/]*");
				i++;
			}
		}
		else if (c == '/')
		{
			pattern += QString("[\\\\/]");
			i++;
		}
		else
		{
			pattern += QRegularExpression::escape(QString(c));
			i++;
		}
	}

	// Member prepend/append rather than the free operator+: the module build sees QString via
	// `export using` from srctrl.qt, which re-exports the type but not Qt's free operators.
	pattern.prepend(QString("\\A"));
	pattern.append(QString("\\z"));
	return QRegularExpression(pattern);
}
