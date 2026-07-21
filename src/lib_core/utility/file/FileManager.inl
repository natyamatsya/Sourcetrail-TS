// Inline implementations for FileManager.h. Included ONLY via SourceGroupSettingsBodies.h (classic: one
// TU emits the weak defs; module build: the srctrl.settings wrapper) -- not by the header itself,
// so the settings family's cross-references stay acyclic. All definitions inline.

#pragma once

// Family-internal includes (srctrl.file), unguarded: same module either way.
#include "FilePath.h"
#include "FilePathFilter.h"
#include "FileSystem.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <set>
#endif

inline FileManager::FileManager() = default;

inline FileManager::~FileManager() = default;

inline void FileManager::update(
	const std::vector<FilePath>& sourcePaths,
	const std::vector<FilePathFilter>& excludeFilters,
	const std::vector<std::string>& sourceExtensions)
{
	m_sourcePaths = sourcePaths;
	m_excludeFilters = excludeFilters;
	m_sourceExtensions = sourceExtensions;

	m_allSourceFilePaths.clear();

	for (const FileInfo& fileInfo:
		 FileSystem::getFileInfosFromPaths(m_sourcePaths, m_sourceExtensions))
	{
		const FilePath& filePath = fileInfo.path;
		if (isExcluded(filePath))
		{
			continue;
		}

		m_allSourceFilePaths.insert(filePath);
	}
}

inline std::vector<FilePath> FileManager::getSourcePaths() const
{
	return m_sourcePaths;
}

inline bool FileManager::hasSourceFilePath(const FilePath& filePath) const
{
	return m_allSourceFilePaths.find(filePath) != m_allSourceFilePaths.end();
}

inline std::set<FilePath> FileManager::getAllSourceFilePaths() const
{
	return m_allSourceFilePaths;
}

inline bool FileManager::isExcluded(const FilePath& filePath) const
{
	return FilePathFilter::areMatching(m_excludeFilters, filePath);
}
