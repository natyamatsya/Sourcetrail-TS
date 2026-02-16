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
	// Replace backslashes with forward slashes first, then escape for regex
	QString filter = QString::fromStdString(filterString);

	// Normalize path separators to forward slash before processing
	filter.replace('\\', '/');

	// Use a placeholder for ** and * before escaping
	static const QString doubleStarPlaceholder = QStringLiteral("\x01\x01");
	static const QString singleStarPlaceholder = QStringLiteral("\x01");

	filter.replace(QStringLiteral("**"), doubleStarPlaceholder);
	filter.replace(QStringLiteral("*"), singleStarPlaceholder);

	// Escape all regex special characters
	filter = QRegularExpression::escape(filter);

	// Replace path separators with pattern matching both / and backslash
	filter.replace(QStringLiteral("/"), QStringLiteral("[\\\\/]"));

	// Restore glob patterns: ** matches anything, * matches within one path segment
	filter.replace(doubleStarPlaceholder, QStringLiteral(".*"));
	filter.replace(singleStarPlaceholder, QStringLiteral("[^\\\\/]*"));

	// Anchor the pattern to match the entire string
	filter = QStringLiteral("\\A") + filter + QStringLiteral("\\z");

	return QRegularExpression(filter);
}
