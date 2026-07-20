#include "SourceGroupZig.h"

#include "FileManager.h"
#include "FilePathFilter.h"
#include "IndexerCommand.h"
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
	// Return the input files that belong to this group (intersection with the
	// group's own source files). NOTE: the base filterToContainedSourceFilePath
	// helper has inverted semantics (it returns the group's files that are NOT
	// in the argument), which would make RefreshInfoGenerator treat every stored
	// file as "removed from the project" and re-index the whole group on every
	// refresh. Compute the intersection directly instead.
	const std::set<FilePath> groupFiles = getAllSourceFilePaths();
	std::set<FilePath> contained;
	for (const FilePath& path: filePaths)
	{
		if (groupFiles.find(path) != groupFiles.end())
		{
			contained.insert(path);
		}
	}
	return contained;
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
	// Zig's per-file incremental model). Emit a command for each of this group's
	// files that is scheduled for indexing — i.e. the intersection of this
	// group's source files with info.filesToIndex. The working directory is the
	// nearest build.zig root so the subprocess (Phase 3b/ZLS) can resolve
	// @import cross-file.
	const std::set<FilePath> groupFiles = getAllSourceFilePaths();

	std::vector<std::shared_ptr<IndexerCommand>> commands;
	for (const FilePath& sourcePath: info.filesToIndex)
	{
		if (groupFiles.find(sourcePath) == groupFiles.end())
			continue;
		FilePath workingDir = findZigRootAtOrAbove(sourcePath);
		if (workingDir.empty())
			workingDir = projectDir;
		commands.push_back(std::make_shared<IndexerCommand>(
			sourcePath, IndexerCommandZig(indexedPaths, workingDir)));
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
