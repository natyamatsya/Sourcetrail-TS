#ifndef FILE_PATH_FILTER_H
#define FILE_PATH_FILTER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include <QRegularExpression>

#include "FilePath.h"
#endif

SRCTRL_EXPORT class FilePathFilter
{
public:
	template<typename ContainerType>
	static bool areMatching(const ContainerType& filters, const FilePath& filePath);

	explicit FilePathFilter(const std::string& filterString);

	std::string str() const;

	bool isMatching(const FilePath& filePath) const;
	bool isMatching(const std::string& fileStr) const;

	bool operator<(const FilePathFilter& other) const;

private:
	static QRegularExpression convertFilterStringToRegex(const std::string& filterString);

	std::string m_filterString;
	QRegularExpression m_filterRegex;
};

template<typename ContainerType>
bool FilePathFilter::areMatching(const ContainerType& filters, const FilePath& filePath)
{
	const std::string fileStr = filePath.str();

	for (const FilePathFilter& filter: filters)
	{
		if (filter.isMatching(fileStr))
		{
			return true;
		}
	}

	return false;
}

#include "FilePathFilter.inl"

#endif	  // FILE_PATH_FILTER_H
