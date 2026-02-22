#include "SourceGroupCxxCMakeFileAPI.h"

#include "ApplicationSettings.h"
#include "CMakeFileAPIReader.h"
#include "CxxIndexerCommandProvider.h"
#include "IndexerCommandCxx.h"
#include "MessageStatus.h"
#include "RefreshInfo.h"
#include "SourceGroupSettingsCxxCMakeFileAPI.h"
#include "SourceGroupSettingsWithCxxCMakeBuildDirectory.h"
#include "TaskLambda.h"
#include "ToolChain.h"
#include "logging.h"
#include "utility.h"
#include "utilitySourceGroupCxx.h"

SourceGroupCxxCMakeFileAPI::SourceGroupCxxCMakeFileAPI(
	std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> settings)
	: m_settings{settings}
{
}

bool SourceGroupCxxCMakeFileAPI::prepareIndexing()
{
	const FilePath sourceDir{m_settings->getSourceDirectoryExpandedAndAbsolute()};
	const std::string& presetName{m_settings->getPresetName()};

	if (sourceDir.empty() || !sourceDir.exists())
	{
		MessageStatus(
			"Can't refresh project. The CMake source directory does not exist: " + sourceDir.str(),
			true)
			.dispatch();
		return false;
	}

	if (presetName.empty())
	{
		MessageStatus("Can't refresh project. No CMake preset configured.", true).dispatch();
		return false;
	}

	MessageStatus("Resolving CMake build directory for preset '" + presetName + "'...", false, true)
		.dispatch();
	const FilePath buildDir{getCachedBuildDir()};
	if (buildDir.empty())
	{
		MessageStatus(
			"Can't refresh project. Failed to resolve build directory for preset '" + presetName +
			"'.",
			true)
			.dispatch();
		return false;
	}

	CMakeFileAPIReader reader{buildDir};
	if (!reader.hasReply() || reader.isReplyStale())
	{
		if (reader.isReplyStale())
		{
			MessageStatus(
				"CMake project has changed — regenerating File API reply...", false, true)
				.dispatch();
			m_cachedBuildDir = {};	// invalidate cached build dir too
		}
		else
		{
			MessageStatus("Writing CMake File API query and running cmake...", false, true)
				.dispatch();
		}

		if (!reader.ensureReply(
				[](const std::string& msg) { MessageStatus(msg, false, true).dispatch(); },
				sourceDir,
				presetName))
		{
			MessageStatus(
				"Can't refresh project. Failed to generate CMake File API reply in: " +
					buildDir.str(),
				true)
				.dispatch();
			return false;
		}
	}
	return true;
}

std::set<FilePath> SourceGroupCxxCMakeFileAPI::filterToContainedFilePaths(
	const std::set<FilePath>& filePaths) const
{
	return SourceGroup::filterToContainedFilePaths(
		filePaths,
		getAllSourceFilePaths(),
		utility::toSet(m_settings->getIndexedHeaderPathsExpandedAndAbsolute()),
		m_settings->getExcludeFiltersExpandedAndAbsolute());
}

std::set<FilePath> SourceGroupCxxCMakeFileAPI::getAllSourceFilePaths() const
{
	const FilePath buildDir{getCachedBuildDir()};
	if (buildDir.empty() || !buildDir.exists())
		return {};

	CMakeFileAPIReader reader{buildDir};
	const auto entries{reader.getSources(
		m_settings->getConfiguration(), m_settings->getTargetGlob())};

	const std::vector<FilePathFilter> excludeFilters{
		m_settings->getExcludeFiltersExpandedAndAbsolute()};

	std::set<FilePath> result{};
	for (const auto& entry : entries)
	{
		if (entry.isGenerated)
			continue;
		if (!entry.path.exists())
			continue;
		if (FilePathFilter::areMatching(excludeFilters, entry.path))
			continue;
		result.insert(entry.path);
	}
	return result;
}

std::shared_ptr<IndexerCommandProvider> SourceGroupCxxCMakeFileAPI::getIndexerCommandProvider(
	const RefreshInfo& info) const
{
	auto provider{std::make_shared<CxxIndexerCommandProvider>()};

	const FilePath buildDir{getCachedBuildDir()};
	if (buildDir.empty() || !buildDir.exists())
		return provider;

	CMakeFileAPIReader reader{buildDir};
	const auto entries{reader.getSources(
		m_settings->getConfiguration(), m_settings->getTargetGlob())};

	const std::vector<FilePathFilter> excludeFilters{
		m_settings->getExcludeFiltersExpandedAndAbsolute()};
	const std::set<FilePath> indexedHeaderPaths{
		utility::toSet(m_settings->getIndexedHeaderPathsExpandedAndAbsolute())};

	const std::vector<std::string> extraFlags{getBaseCompilerFlags()};

	for (const auto& entry : entries)
	{
		if (entry.isGenerated)
			continue;
		if (!entry.path.exists())
			continue;
		if (FilePathFilter::areMatching(excludeFilters, entry.path))
			continue;
		if (info.filesToIndex.find(entry.path) == info.filesToIndex.end())
			continue;

		// Build the compiler command line from the CMake File API compile group.
		std::vector<std::string> commandLine{};

		if (entry.compileGroup)
		{
			const auto& cg{*entry.compileGroup};

			// Language standard flag derived from the language reported by CMake.
			if (cg.language == "CXX")
			{
				commandLine.push_back(ClangCompiler::languageOption());
				commandLine.push_back(ClangCompiler::CPP_LANGUAGE);
			}
			else if (cg.language == "C")
			{
				commandLine.push_back(ClangCompiler::languageOption());
				commandLine.push_back(ClangCompiler::C_LANGUAGE);
			}

			// Include paths.
			for (const auto& inc : cg.includes)
				commandLine.push_back("-I" + inc.str());
			for (const auto& inc : cg.systemIncludes)
				commandLine.push_back("-isystem" + inc.str());

			// Preprocessor defines.
			for (const auto& def : cg.defines)
				commandLine.push_back("-D" + def);

			// Extra compiler fragments from CMake (e.g. -std=c++17, -fPIC).
			for (const auto& flag : cg.compileFlags)
				commandLine.push_back(flag);
		}

		// Append global extra flags (ApplicationSettings header/framework paths).
		utility::append(commandLine, extraFlags);

		// Append user-specified extra flags from settings.
		utility::append(commandLine, m_settings->getCompilerFlags());

		// Source file itself.
		commandLine.push_back(entry.path.str());

		provider->addCommand(std::make_shared<IndexerCommandCxx>(
			entry.path,
			utility::concat(indexedHeaderPaths, {entry.path}),
			utility::toSet(excludeFilters),
			std::set<FilePathFilter>{},
			buildDir,
			std::move(commandLine)));
	}

	provider->logStats();
	return provider;
}

std::vector<std::shared_ptr<IndexerCommand>> SourceGroupCxxCMakeFileAPI::getIndexerCommands(
	const RefreshInfo& info) const
{
	return getIndexerCommandProvider(info)->consumeAllCommands();
}

FilePath SourceGroupCxxCMakeFileAPI::getCachedBuildDir() const
{
	if (m_cachedBuildDir.empty())
		m_cachedBuildDir = m_settings->resolveBuildDirectory();
	return m_cachedBuildDir;
}

std::shared_ptr<SourceGroupSettings> SourceGroupCxxCMakeFileAPI::getSourceGroupSettings()
{
	return m_settings;
}

std::shared_ptr<const SourceGroupSettings> SourceGroupCxxCMakeFileAPI::getSourceGroupSettings() const
{
	return m_settings;
}

std::vector<std::string> SourceGroupCxxCMakeFileAPI::getBaseCompilerFlags() const
{
	std::vector<std::string> flags{};
	const auto appSettings{ApplicationSettings::getInstance()};

	utility::append(
		flags,
		IndexerCommandCxx::getCompilerFlagsForSystemHeaderSearchPaths(utility::concat(
			m_settings->getHeaderSearchPathsExpandedAndAbsolute(),
			appSettings->getHeaderSearchPathsExpanded())));

	utility::append(
		flags,
		IndexerCommandCxx::getCompilerFlagsForFrameworkSearchPaths(utility::concat(
			m_settings->getFrameworkSearchPathsExpandedAndAbsolute(),
			appSettings->getFrameworkSearchPathsExpanded())));

	return flags;
}
