#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <map>
#include <set>
#include <string>
#include <vector>
#endif

// Same-module fwd decls (srctrl.file): fine in the purview too -- FilePath/FilePathFilter are
// declared earlier in the wrapper's include order.
class FilePath;
class FilePathFilter;

SRCTRL_EXPORT class FileManager
{
public:
	FileManager();
	virtual ~FileManager();

	void update(
		const std::vector<FilePath>& sourcePaths,
		const std::vector<FilePathFilter>& excludeFilters,
		const std::vector<std::string>& sourceExtensions);

	// returns a list of source paths (can be directories) specified in the project settings
	std::vector<FilePath> getSourcePaths() const;

	// checks if file is in non-excluded source directory
	bool hasSourceFilePath(const FilePath& filePath) const;

	// returns a list of paths to all files that reside in the non-excluded source paths
	std::set<FilePath> getAllSourceFilePaths() const;

private:
	bool isExcluded(const FilePath& filePath) const;

	std::vector<FilePath> m_sourcePaths;
	std::vector<FilePathFilter> m_excludeFilters;
	std::vector<std::string> m_sourceExtensions;

	std::set<FilePath> m_allSourceFilePaths;
};

#include "FileManager.inl"

#endif	  // FILE_MANAGER_H
