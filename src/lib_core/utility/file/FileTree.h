#ifndef FILE_TREE_H
#define FILE_TREE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <set>
#include <string>
#include <unordered_map>

#include "FilePath.h"
#endif

SRCTRL_EXPORT class FileTree
{
public:
	FileTree(const FilePath& rootPath);

	FilePath getAbsoluteRootPathForRelativeFilePath(const FilePath& relativeFilePath);
	std::vector<FilePath> getAbsoluteRootPathsForRelativeFilePath(const FilePath& relativeFilePath);

private:
	std::vector<FilePath> doGetAbsoluteRootPathsForRelativeFilePath(
		const FilePath& relativeFilePath, bool allowMultipleResults);

	FilePath m_rootPath;
	std::unordered_map<std::string, std::set<FilePath>> m_files;
};

#include "FileTree.inl"

#endif	  // FILE_TREE_H
