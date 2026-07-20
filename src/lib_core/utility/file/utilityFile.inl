// Inline implementations for utilityFile.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <filesystem>

#include "FileSystem.h"
#include "logging.h"
#include "utility.h"
#endif

inline std::vector<FilePath> utility::partitionFilePathsBySize(std::vector<FilePath> filePaths, int partitionCount)
{
	typedef std::pair<unsigned long long int, FilePath> PairType;
	std::vector<PairType> sourceFileSizesToCommands;
	for (const FilePath& path: filePaths)
	{
		if (!path.exists() || path.isDirectory())
		{
			sourceFileSizesToCommands.push_back(std::make_pair(1, path));
			continue;
		}

		try
		{
			sourceFileSizesToCommands.push_back(
				std::make_pair(FileSystem::getFileByteSize(path), path));
		}
		catch (const std::filesystem::filesystem_error& e)
		{
			LOG_ERROR_STREAM(
				<< "cannot read file size for partitioning: " << path.str() << " ("
				<< e.what() << ")");
			sourceFileSizesToCommands.push_back(std::make_pair(1, path));
		}
	}

	std::sort(
		sourceFileSizesToCommands.begin(),
		sourceFileSizesToCommands.end(),
		[](const PairType& p, const PairType& q) { return p.first > q.first; });

	if (0 < partitionCount && partitionCount < static_cast<int>(sourceFileSizesToCommands.size()))
	{
		for (int i = 0; i < partitionCount; i++)
		{
			std::sort(
				sourceFileSizesToCommands.begin() +
					sourceFileSizesToCommands.size() * i / partitionCount,
				sourceFileSizesToCommands.begin() +
					sourceFileSizesToCommands.size() * (i + 1) / partitionCount,
				[](const PairType& p, const PairType& q) {
					return p.second.str() < q.second.str();
				});
		}
	}

	std::vector<FilePath> sortedFilePaths;
	for (const PairType& pair: sourceFileSizesToCommands)
	{
		sortedFilePaths.push_back(pair.second);
	}
	return sortedFilePaths;
}

inline std::vector<FilePath> utility::getTopLevelPaths(const std::vector<FilePath>& paths)
{
	return utility::getTopLevelPaths(utility::toSet(paths));
}

inline std::vector<FilePath> utility::getTopLevelPaths(const std::set<FilePath>& paths)
{
	// this works because the set contains the paths already in alphabetical order
	std::vector<FilePath> topLevelPaths;

	FilePath lastPath;
	for (const FilePath& path: paths)
	{
		if (lastPath.empty() ||
			!lastPath.contains(path))	 // don't add subdirectories of already added paths
		{
			lastPath = path;
			topLevelPaths.push_back(path);
		}
	}

	return topLevelPaths;
}

inline FilePath utility::getExpandedPath(const FilePath& path)
{
	std::vector<FilePath> paths = path.expandEnvironmentVariables();
	if (!paths.empty())
	{
		if (paths.size() > 1)
		{
			LOG_WARNING(
				"Environment variable in path \"" + path.str() + "\" has been expanded to " +
				std::to_string(paths.size()) + "paths, but only \"" + paths.front().str() +
				"\" will be used.");
		}
		return paths.front();
	}
	return FilePath();
}

inline std::vector<FilePath> utility::getExpandedPaths(const std::vector<FilePath>& paths)
{
	std::vector<FilePath> expandedPaths;
	for (const FilePath& path: paths)
	{
		utility::append(expandedPaths, path.expandEnvironmentVariables());
	}
	return expandedPaths;
}

inline FilePath utility::getExpandedAndAbsolutePath(const FilePath& path, const FilePath& baseDirectory)
{
	FilePath p = getExpandedPath(path);

	if (p.empty() || p.isAbsolute())
	{
		return p;
	}

	return baseDirectory.getConcatenated(p).makeCanonical();
}

inline FilePath utility::getAsRelativeIfShorter(const FilePath& absolutePath, const FilePath& baseDirectory)
{
	if (!baseDirectory.empty())
	{
		const FilePath relativePath = absolutePath.getRelativeTo(baseDirectory);
		if (relativePath.str().size() < absolutePath.str().size())
		{
			return relativePath;
		}
	}
	return absolutePath;
}

inline std::vector<FilePath> utility::getAsRelativeIfShorter(
	const std::vector<FilePath>& absolutePaths, const FilePath& baseDirectory)
{
	return utility::convert<FilePath, FilePath>(absolutePaths, [&](const FilePath& path) {
		return getAsRelativeIfShorter(path, baseDirectory);
	});
}
