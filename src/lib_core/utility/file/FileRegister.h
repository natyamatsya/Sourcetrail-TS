#ifndef FILE_REGISTER_H
#define FILE_REGISTER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <set>

#include "FilePath.h"
#include "UnorderedCache.h"

class FilePathFilter;
#endif

SRCTRL_EXPORT class FileRegister
{
public:
	FileRegister(
		const FilePath& currentPath,
		const std::set<FilePath>& indexedPaths,
		const std::set<FilePathFilter>& excludeFilters);
	virtual ~FileRegister();

	virtual bool hasFilePath(const FilePath& filePath) const;

private:
	const FilePath& m_currentPath;
	const std::set<FilePath> m_indexedPaths;
	const std::set<FilePathFilter> m_excludeFilters;
	mutable UnorderedCache<std::string, bool> m_hasFilePathCache;
};

#include "FileRegister.inl"

#endif	  // FILE_REGISTER_H
