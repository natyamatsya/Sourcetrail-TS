#include "FilePathFilter.h"

#include <QString>

FilePathFilter::FilePathFilter(const std::string& filterString)
	: m_filterString(filterString), m_filterRegex(convertFilterStringToRegex(filterString))
{
}

std::string FilePathFilter::str() const
{
	return m_filterString;
}

bool FilePathFilter::isMatching(const FilePath& filePath) const
{
	return isMatching(filePath.str());
}

bool FilePathFilter::isMatching(const std::string& fileStr) const
{
	return m_filterRegex.match(QString::fromStdString(fileStr)).hasMatch();
}

bool FilePathFilter::operator<(const FilePathFilter& other) const
{
	return m_filterString.compare(other.m_filterString) < 0;
}

QRegularExpression FilePathFilter::convertFilterStringToRegex(const std::string& filterString)
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
				pattern += QStringLiteral(".*");
				i += 2;
			}
			else
			{
				pattern += QStringLiteral("[^\\\\/]*");
				i++;
			}
		}
		else if (c == '/')
		{
			pattern += QStringLiteral("[\\\\/]");
			i++;
		}
		else
		{
			pattern += QRegularExpression::escape(QString(c));
			i++;
		}
	}

	return QRegularExpression(QStringLiteral("\\A") + pattern + QStringLiteral("\\z"));
}
