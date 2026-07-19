#include "SourceGroupZig.h"

#include "FileManager.h"
#include "IndexerCommandZig.h"
#include "RefreshInfo.h"
#include "SourceGroupSettingsZigEmpty.h"
#include "utility.h"

namespace
{
// Nearest directory at or above `path` that contains a build.zig — the Zig
// project root the subprocess resolves imports against. Empty when none is
// found (a loose collection of .zig files); the file's own directory is used.
FilePath findZigRootAtOrAbove(FilePath path)
{
	if (!path.isDirectory())
		path = path.getParentDirectory();
	while (!path.empty())
	{
		if (path.getConcatenated("/build.zig").exists())
			return path;
		const FilePath parent = path.getParentDirectory();
		if (parent == path)
			break;
		path = parent;
	}
	return FilePath();
}
}	 // namespace

SourceGroupZig::SourceGroupZig(const std::shared_ptr<SourceGroupSettingsZigEmpty>& settings)
	: m_settings{settings}
{
}

std::expected<void, PrepareIndexingError> SourceGroupZig::prepareIndexing()
{
	return {};
}

std::set<FilePath> SourceGroupZig::filterToContainedFilePaths(
	const std::set<FilePath>& filePaths) const
{
	return filterToContainedSourceFilePath(filePaths);
}

std::set<FilePath> SourceGroupZig::getAllSourceFilePaths() const
{
	FileManager fileManager;
	fileManager.update(
		m_settings->getSourcePathsExpandedAndAbsolute(),
		m_settings->getExcludeFiltersExpandedAndAbsolute(),
		m_settings->getSourceExtensions());
	return fileManager.getAllSourceFilePaths();
}

std::vector<std::shared_ptr<IndexerCommand>> SourceGroupZig::getIndexerCommands(
	const RefreshInfo& info) const
{
	const std::set<FilePath> indexedPaths =
		utility::toSet(m_settings->getSourcePathsExpandedAndAbsolute());
	const FilePath projectDir = m_settings->getProjectDirectoryPath();

	// The Zig indexer parses one file per command (the natural granularity for
	// Zig's per-file incremental model). Emit a command for each in-scope file
	// scheduled for indexing; the working directory is the nearest build.zig
	// root so the subprocess (Phase 3b/ZLS) can resolve @import cross-file.
	std::vector<std::shared_ptr<IndexerCommand>> commands;
	for (const FilePath& sourcePath: filterToContainedSourceFilePath(info.filesToIndex))
	{
		FilePath workingDir = findZigRootAtOrAbove(sourcePath);
		if (workingDir.empty())
			workingDir = projectDir;
		commands.push_back(
			std::make_shared<IndexerCommandZig>(sourcePath, indexedPaths, workingDir));
	}
	return commands;
}

std::shared_ptr<SourceGroupSettings> SourceGroupZig::getSourceGroupSettings()
{
	return m_settings;
}

std::shared_ptr<const SourceGroupSettings> SourceGroupZig::getSourceGroupSettings() const
{
	return m_settings;
}
