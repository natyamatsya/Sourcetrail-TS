#include "SourceGroupSwift.h"

#include <set>

#include "FileManager.h"
#include "FileSystem.h"
#include "IndexerCommandSwift.h"
#include "RefreshInfo.h"
#include "SourceGroupSettingsSwiftEmpty.h"
#include "logging.h"
#include "utility.h"

namespace
{
//! Swift crate fan-out: the SPM package roots (directories containing a
//! Package.swift) at or under the indexed source paths. One command per root
//! lets K Swift supervisors index packages concurrently and, more basically,
//! makes `swift build` run in a directory that actually has a manifest.
//! Empty result = no manifest found; the caller falls back to a single
//! whole-directory command (which the subprocess degrades to a synthetic
//! single-module scan).
std::set<FilePath> enumeratePackageRoots(const std::set<FilePath>& indexedPaths)
{
	std::set<FilePath> packageRoots;
	for (const FilePath& indexedPath: indexedPaths)
	{
		if (!indexedPath.isDirectory())
		{
			if (indexedPath.fileName() == "Package.swift")
				packageRoots.insert(indexedPath.getParentDirectory());
			continue;
		}
		// getFilePathsFromDirectory recurses, so a single .swift scan of the
		// indexed root finds a manifest at the root and in every nested
		// package (multi-package repos / monorepos).
		for (const FilePath& manifest:
			 FileSystem::getFilePathsFromDirectory(indexedPath, {".swift"}))
		{
			if (manifest.fileName() == "Package.swift")
				packageRoots.insert(manifest.getParentDirectory());
		}
	}
	return packageRoots;
}
}  // namespace

SourceGroupSwift::SourceGroupSwift(const std::shared_ptr<SourceGroupSettingsSwiftEmpty>& settings)
	: m_settings{settings}
{
}

std::expected<void, PrepareIndexingError> SourceGroupSwift::prepareIndexing()
{
	return {};
}

std::set<FilePath> SourceGroupSwift::filterToContainedFilePaths(
	const std::set<FilePath>& filePaths) const
{
	return filterToContainedSourceFilePath(filePaths);
}

std::set<FilePath> SourceGroupSwift::getAllSourceFilePaths() const
{
	FileManager fileManager;
	fileManager.update(
		m_settings->getSourcePathsExpandedAndAbsolute(),
		m_settings->getExcludeFiltersExpandedAndAbsolute(),
		m_settings->getSourceExtensions());
	return fileManager.getAllSourceFilePaths();
}

std::vector<std::shared_ptr<IndexerCommand>> SourceGroupSwift::getIndexerCommands(
	const RefreshInfo& info) const
{
	const std::set<FilePath> indexedPaths =
		utility::toSet(m_settings->getSourcePathsExpandedAndAbsolute());
	const FilePath workingDir = m_settings->getProjectDirectoryPath();

	if (info.filesToIndex.empty())
		return {};

	// Swift project-model options (SW5): extra build args, a toolchain override,
	// and an index-store override travel with every command to the subprocess.
	const std::vector<std::string> buildArgs = m_settings->getSwiftBuildArgs();
	const std::string toolchainPath = m_settings->getSwiftToolchainPathExpandedAndAbsolute().str();
	const std::string indexStorePath = m_settings->getSwiftIndexStorePathExpandedAndAbsolute().str();
	const std::string specializationScope = m_settings->getSwiftSpecializationScope();

	auto makeCommand = [&](const FilePath& packageRoot) {
		// The source file path doubles as the status-tracking key.
		return std::make_shared<IndexerCommandSwift>(
			packageRoot, indexedPaths, packageRoot, buildArgs, toolchainPath, indexStorePath,
			specializationScope);
	};

	// One command per SPM package root, so K Swift supervisors index packages
	// concurrently and each `swift build` runs where a manifest exists. When
	// no manifest is found, fall back to a single whole-directory command —
	// the subprocess degrades it to a synthetic single-module scan.
	std::vector<std::shared_ptr<IndexerCommand>> commands;
	for (const FilePath& packageRoot: enumeratePackageRoots(indexedPaths))
		commands.push_back(makeCommand(packageRoot));
	if (commands.empty())
		commands.push_back(makeCommand(workingDir));
	return commands;
}

std::shared_ptr<SourceGroupSettings> SourceGroupSwift::getSourceGroupSettings()
{
	return m_settings;
}

std::shared_ptr<const SourceGroupSettings> SourceGroupSwift::getSourceGroupSettings() const
{
	return m_settings;
}
