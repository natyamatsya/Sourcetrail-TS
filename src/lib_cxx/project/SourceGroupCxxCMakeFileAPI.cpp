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
#include "utilityString.h"
#include "utilitySourceGroupCxx.h"
#include "utilityApp.h"
#include "utilityClang.h"

#include <fstream>

namespace
{

bool isHeaderFilePath(const FilePath& path)
{
	static const std::set<std::string> kHeaderExtensions{
		".h", ".hh", ".hpp", ".hxx", ".inl", ".inc"};
	return kHeaderExtensions.count(utility::toLowerCase(path.extension())) > 0;
}

bool isModuleInterfaceFilePath(const FilePath& path)
{
	static const std::set<std::string> kModuleInterfaceExtensions{
		".ixx", ".cppm", ".cxxm", ".ccm", ".mxx", ".mpp"};
	return kModuleInterfaceExtensions.count(utility::toLowerCase(path.extension())) > 0;
}

bool isIndexableCxxFilePath(const FilePath& path)
{
	static const std::set<std::string> kIndexableExtensions{
		".c",
		".cc",
		".cpp",
		".cxx",
		".m",
		".mm",
		".h",
		".hh",
		".hpp",
		".hxx",
		".inl",
		".inc",
		".ixx",
		".cppm",
		".cxxm",
		".ccm",
		".mxx",
		".mpp"};
	return kIndexableExtensions.count(utility::toLowerCase(path.extension())) > 0;
}

bool hasExplicitLanguageFlag(const std::vector<std::string>& flags)
{
	const std::string languageOption{ClangCompiler::languageOption()};
	for (const std::string& flag : flags)
		if (utility::isPrefix(languageOption, flag))
			return true;
	return false;
}

BuildLanguage toBuildLanguage(const std::string& language)
{
	if (language == "C")
		return BuildLanguage::C;
	if (language == "CXX")
		return BuildLanguage::CXX;
	if (language == "RUST")
		return BuildLanguage::RUST;
	return BuildLanguage::UNKNOWN;
}

BuildTargetKind toBuildTargetKind(const std::string& targetType)
{
	if (targetType == "EXECUTABLE")
		return BuildTargetKind::EXECUTABLE;
	if (targetType == "STATIC_LIBRARY")
		return BuildTargetKind::STATIC_LIBRARY;
	if (targetType == "SHARED_LIBRARY")
		return BuildTargetKind::SHARED_LIBRARY;
	if (targetType == "MODULE_LIBRARY")
		return BuildTargetKind::MODULE_LIBRARY;
	if (targetType == "OBJECT_LIBRARY")
		return BuildTargetKind::OBJECT_LIBRARY;
	if (targetType == "INTERFACE_LIBRARY")
		return BuildTargetKind::INTERFACE_LIBRARY;
	if (targetType == "UTILITY")
		return BuildTargetKind::UTILITY;
	if (targetType == "CUSTOM")
		return BuildTargetKind::CUSTOM;
	return BuildTargetKind::UNKNOWN;
}

BuildModelIssueCode toBuildModelIssueCode(const CMakeFileAPIReader::GetSourcesErrorCode& code)
{
	switch (code)
	{
	case CMakeFileAPIReader::GetSourcesErrorCode::ReplyIndexNotFound:
		return BuildModelIssueCode::BUILD_REPLY_NOT_FOUND;
	case CMakeFileAPIReader::GetSourcesErrorCode::ReplyIndexUnreadable:
	case CMakeFileAPIReader::GetSourcesErrorCode::CodemodelUnreadable:
		return BuildModelIssueCode::BUILD_REPLY_UNREADABLE;
	case CMakeFileAPIReader::GetSourcesErrorCode::CodemodelReferenceMissing:
	case CMakeFileAPIReader::GetSourcesErrorCode::CodemodelParseError:
	case CMakeFileAPIReader::GetSourcesErrorCode::CodemodelRootNotObject:
	case CMakeFileAPIReader::GetSourcesErrorCode::CodemodelUnexpectedSchema:
		return BuildModelIssueCode::BUILD_REPLY_MALFORMED;
	case CMakeFileAPIReader::GetSourcesErrorCode::ConfigurationNotFound:
		return BuildModelIssueCode::CONFIGURATION_NOT_FOUND;
	case CMakeFileAPIReader::GetSourcesErrorCode::AllMatchedTargetsUnreadable:
		return BuildModelIssueCode::SOURCES_UNAVAILABLE;
	}
	return BuildModelIssueCode::UNKNOWN;
}

BuildModelIssueCode toBuildModelIssueCode(const CMakeFileAPIReader::GetSourcesWarningCode& code)
{
	switch (code)
	{
	case CMakeFileAPIReader::GetSourcesWarningCode::NestedTargetReferenceArraysFlattened:
	case CMakeFileAPIReader::GetSourcesWarningCode::TargetRootArrayNormalized:
	case CMakeFileAPIReader::GetSourcesWarningCode::TargetKeyValueArrayNormalized:
		return BuildModelIssueCode::DATA_NORMALIZED;
	case CMakeFileAPIReader::GetSourcesWarningCode::MalformedTargetReference:
		return BuildModelIssueCode::TARGET_REFERENCE_MALFORMED;
	case CMakeFileAPIReader::GetSourcesWarningCode::TargetMissingJsonFile:
		return BuildModelIssueCode::TARGET_METADATA_MISSING;
	case CMakeFileAPIReader::GetSourcesWarningCode::TargetReplyUnreadable:
		return BuildModelIssueCode::TARGET_METADATA_UNREADABLE;
	}
	return BuildModelIssueCode::UNKNOWN;
}

std::optional<BuildCompileGroupSnapshot> toBuildCompileGroupSnapshot(
	const std::optional<CMakeFileAPIReader::CompileGroup>& compileGroup)
{
	if (!compileGroup)
		return std::nullopt;

	BuildCompileGroupSnapshot snapshot{};
	snapshot.language = toBuildLanguage(compileGroup->language);
	snapshot.compilerPath = compileGroup->compilerPath;
	snapshot.sysroot = compileGroup->sysroot;
	snapshot.includes = compileGroup->includes;
	snapshot.systemIncludes = compileGroup->systemIncludes;
	snapshot.frameworkSearchPaths = compileGroup->frameworkSearchPaths;
	snapshot.defines = compileGroup->defines;
	snapshot.flags = compileGroup->compileFlags;
	return snapshot;
}

bool shouldCreateCxxCommand(
	const CMakeFileAPIReader::SourceEntry& entry,
	const std::vector<FilePathFilter>& excludeFilters)
{
	if (entry.isGenerated)
		return false;
	if (!entry.path.exists())
		return false;
	if (FilePathFilter::areMatching(excludeFilters, entry.path))
		return false;
	if (!isIndexableCxxFilePath(entry.path))
		return false;
	if ((isHeaderFilePath(entry.path) || isModuleInterfaceFilePath(entry.path)) &&
		!entry.compileGroup)
		return false;
	return true;
}

// Hybrid C++20 module dependency injection.
//
// CMake generates per-TU response files (<buildDir>/CMakeFiles/<target>.dir/<rel>.o.modmap)
// containing the exact -fmodule-file= and -x c++-module flags needed to resolve module
// dependencies.  When a build exists we parse those flags and expand relative paths to
// absolute ones so libclang can locate the .pcm files.
//
// If no .modmap exists (no prior build, or build was cleaned) we fall back to injecting
// -fprebuilt-module-path= pointing at the target's CMakeFiles directory, which lets
// libclang pick up any .pcm files that happen to be present without hard-coding names.
std::vector<std::string> getModuleFlags(
	const CMakeFileAPIReader::SourceEntry& entry, const FilePath& buildDir)
{
	// Derive modmap path: <buildDir>/CMakeFiles/<targetName>.dir/<relSrcPath>.o.modmap
	const FilePath relSrcPath{entry.path.getRelativeTo(entry.sourceDir)};
	const FilePath targetDir{
		buildDir.getConcatenated("/CMakeFiles/" + entry.targetName + ".dir")};
	const FilePath modmapPath{
		targetDir.getConcatenated("/" + relSrcPath.str() + ".o.modmap")};

	if (modmapPath.exists())
	{
		// Primary path: parse the .modmap response file.
		std::vector<std::string> flags{};
		std::ifstream file{modmapPath.str()};
		std::string line{};
		while (std::getline(file, line))
		{
			if (line.empty())
				continue;

			// Handle space-separated two-token flags like "-x c++-module".
			// A line is a two-token flag when it starts with '-', has a space, and
			// the part before the space has no '=' (it's a flag name, not a value).
			const auto spacePos{line.find(' ')};
			if (spacePos != std::string::npos && line.find('=') == std::string::npos)
			{
				flags.push_back(line.substr(0, spacePos));
				flags.push_back(line.substr(spacePos + 1));
				continue;
			}

			// For flags of the form -ffoo=<value> (or -ffoo=name=<path> for
			// -fmodule-file=), expand relative path values to absolute.
			// Use the *last* '=' to find the path component so that
			// -fmodule-file=hello=rel/path works correctly.
			const auto lastEqPos{line.rfind('=')};
			if (lastEqPos != std::string::npos)
			{
				const std::string value{line.substr(lastEqPos + 1)};
				// Only expand if the value looks like a file path (contains a separator
				// or ends with a known extension), not a bare name like "hello".
				if (!value.empty() && !FilePath{value}.isAbsolute() &&
					(value.find('/') != std::string::npos ||
					 value.size() > 4 &&
						 value.substr(value.size() - 4) == ".pcm"))
				{
					const FilePath absValue{buildDir.getConcatenated("/" + value)};
					flags.push_back(line.substr(0, lastEqPos + 1) + absValue.str());
					continue;
				}
			}
			flags.push_back(line);
		}
		LOG_INFO(
			"C++20 module flags from .modmap (" + std::to_string(flags.size()) +
			" flags): " + modmapPath.str());
		return flags;
	}

	// Fallback: no .modmap available.  Inject -fprebuilt-module-path= pointing
	// at the target's CMakeFiles dir so libclang can find any pre-built .pcm files.
	if (targetDir.exists())
	{
		LOG_INFO(
			"C++20 module fallback: -fprebuilt-module-path=" + targetDir.str() +
			" (no .modmap found at " + modmapPath.str() + ")");
		return {"-fprebuilt-module-path=" + targetDir.str()};
	}
	return {};
}
}

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
	const auto entriesResult{reader.getSourcesDetailed(
		m_settings->getConfiguration(), m_settings->getTargetGlob())};
	if (!entriesResult.has_value())
	{
		const auto& error{entriesResult.error()};
		const std::string errorCode{
			CMakeFileAPIReader::getSourcesErrorCodeToString(error.code)};
		MessageStatus(
			"Can't refresh project. Failed to read CMake File API sources (" +
				errorCode + "): " + error.message,
			true)
			.dispatch();
		LOG_WARNING(
			"SourceGroupCxxCMakeFileAPI: getSourcesDetailed failed code='" + errorCode +
			"' message='" + error.message + "'");
		return {};
	}

	const auto& entries{entriesResult->entries};

	const std::vector<FilePathFilter> excludeFilters{
		m_settings->getExcludeFiltersExpandedAndAbsolute()};

	std::set<FilePath> result{};
	for (const auto& entry : entries)
	{
		if (!shouldCreateCxxCommand(entry, excludeFilters))
			continue;
		result.insert(entry.path);
	}
	return result;
}

std::optional<BuildModelSnapshot> SourceGroupCxxCMakeFileAPI::getBuildModelSnapshot() const
{
	const FilePath buildDir{getCachedBuildDir()};
	if (buildDir.empty() || !buildDir.exists())
		return std::nullopt;

	BuildModelSnapshot snapshot{};
	snapshot.provider = BuildModelProvider::CMAKE_FILE_API;
	snapshot.configuration = m_settings->getConfiguration();
	snapshot.targetGlob = m_settings->getTargetGlob();
	snapshot.buildDir = buildDir;

	CMakeFileAPIReader reader{buildDir};
	for (const auto& entryPoint : reader.getJsonEntryPoints())
		snapshot.jsonEntryPoints.push_back({entryPoint.kind, entryPoint.path});

	const auto entriesResult{reader.getSourcesDetailed(snapshot.configuration, snapshot.targetGlob)};
	if (!entriesResult.has_value())
	{
		const auto& error{entriesResult.error()};
		snapshot.health = BuildModelHealth::FAILED;
		snapshot.issues.push_back(
			{BuildModelIssueSeverity::ERROR,
				toBuildModelIssueCode(error.code),
				{},
				buildDir,
				error.message});
		return snapshot;
	}

	const auto& detailedResult{entriesResult.value()};
	snapshot.targetCount = detailedResult.targetCount;
	snapshot.normalizedTargetCount = detailedResult.normalizedTargetCount;
	snapshot.matchedTargetCount = detailedResult.matchedTargetCount;
	snapshot.malformedTargetReferenceCount = detailedResult.malformedTargetReferenceCount;
	snapshot.emptyTargetReplyCount = detailedResult.emptyTargetReplyCount;
	snapshot.unreadableTargetReplyCount = detailedResult.unreadableTargetReplyCount;
	snapshot.sourceObjectCount = detailedResult.sourceObjectCount;
	snapshot.duplicateSourceCount = detailedResult.duplicateSourceCount;

	for (const auto& warning : detailedResult.warnings)
		snapshot.issues.push_back(
			{BuildModelIssueSeverity::WARNING,
				toBuildModelIssueCode(warning.code),
				warning.targetName,
				warning.path,
				CMakeFileAPIReader::getSourcesWarningCodeToString(warning.code)});

	for (const auto& entry : detailedResult.entries)
	{
		BuildFileSnapshot file{};
		file.path = entry.path;
		file.isGenerated = entry.isGenerated;
		file.targetName = entry.targetName;
		file.targetType = entry.targetType;
		file.sourceDir = entry.sourceDir;
		file.compileGroup = toBuildCompileGroupSnapshot(entry.compileGroup);
		snapshot.files.push_back(std::move(file));

		bool foundTarget{false};
		for (auto& target : snapshot.targets)
			if (target.name == entry.targetName)
			{
				++target.fileCount;
				foundTarget = true;
				break;
			}
		if (foundTarget)
			continue;

		BuildTargetSnapshot target{};
		target.name = entry.targetName;
		target.kind = toBuildTargetKind(entry.targetType);
		target.sourceDir = entry.sourceDir;
		target.fileCount = 1;
		snapshot.targets.push_back(std::move(target));
	}

	if (!snapshot.issues.empty())
		snapshot.health = BuildModelHealth::PARTIAL;

	return snapshot;
}

std::shared_ptr<IndexerCommandProvider> SourceGroupCxxCMakeFileAPI::getIndexerCommandProvider(
	const RefreshInfo& info) const
{
	auto provider{std::make_shared<CxxIndexerCommandProvider>()};

	const FilePath buildDir{getCachedBuildDir()};
	if (buildDir.empty() || !buildDir.exists())
		return provider;

	CMakeFileAPIReader reader{buildDir};
	const auto entriesResult{reader.getSourcesDetailed(
		m_settings->getConfiguration(), m_settings->getTargetGlob())};
	if (!entriesResult.has_value())
	{
		const auto& error{entriesResult.error()};
		const std::string errorCode{
			CMakeFileAPIReader::getSourcesErrorCodeToString(error.code)};
		LOG_WARNING(
			"SourceGroupCxxCMakeFileAPI: getSourcesDetailed failed code='" + errorCode +
			"' message='" + error.message + "'");
		return provider;
	}

	const auto& entries{entriesResult->entries};

	const std::vector<FilePathFilter> excludeFilters{
		m_settings->getExcludeFiltersExpandedAndAbsolute()};
	const std::set<FilePath> indexedHeaderPaths{
		utility::toSet(m_settings->getIndexedHeaderPathsExpandedAndAbsolute())};

	const std::vector<std::string> extraFlags{getBaseCompilerFlags()};

	for (const auto& entry : entries)
	{
		if (!shouldCreateCxxCommand(entry, excludeFilters))
			continue;
		if (info.filesToIndex.find(entry.path) == info.filesToIndex.end())
			continue;

		// Build the compiler command line from the CMake File API compile group.
		std::vector<std::string> commandLine{};

		if (entry.compileGroup)
		{
			const auto& cg{*entry.compileGroup};
			const bool hasLanguageFlag{hasExplicitLanguageFlag(cg.compileFlags)};

			// Language standard flag derived from the language reported by CMake.
			if (!hasLanguageFlag && cg.language == "CXX")
			{
				commandLine.push_back(ClangCompiler::languageOption());
				commandLine.push_back(ClangCompiler::CPP_LANGUAGE);
			}
			else if (!hasLanguageFlag && cg.language == "C")
			{
				commandLine.push_back(ClangCompiler::languageOption());
				commandLine.push_back(ClangCompiler::C_LANGUAGE);
			}

			// Include paths.
			for (const auto& inc : cg.includes)
				commandLine.push_back("-I" + inc.str());

			for (const auto& inc : cg.systemIncludes)
				commandLine.push_back("-isystem" + inc.str());
			for (const auto& frameworkPath : cg.frameworkSearchPaths)
			{
				commandLine.push_back(ClangCompiler::frameworkIncludeOption());
				commandLine.push_back(frameworkPath.str());
			}

			// Preprocessor defines.
			for (const auto& def : cg.defines)
				commandLine.push_back("-D" + def);

			// Extra compiler fragments from CMake (e.g. -std=c++17, -fPIC).
			for (const auto& flag : cg.compileFlags)
				commandLine.push_back(flag);
		}

		// Append global extra flags (ApplicationSettings header/framework paths)
		// only when the compiler lacks a usable resource dir — the driver
		// handles them implicitly when it has one.
		if (!entry.compileGroup || !utility::resolveCompilerResourceDir(entry.compileGroup->compilerPath))
			utility::append(commandLine, extraFlags);

		// Fallback: If no sysroot was explicitly provided in CMake flags but we found an implicit one, inject it.
		// (CMake compileCommandFragments usually doesn't include -isysroot if it assumes the compiler does it natively,
		// but since we are replacing the compiler with our indexer, we might need it explicitly.)
		std::string sysrootToInject;
		if (entry.compileGroup && !entry.compileGroup->sysroot.empty())
		{
			sysrootToInject = entry.compileGroup->sysroot.str();
		}
		else if constexpr (utility::Platform::isMac())
		{
			// If CMake didn't capture a sysroot, fallback to xcrun on macOS.
			const utility::ProcessOutput output = utility::executeProcess("xcrun", {"--show-sdk-path"});
			if (output.exitCode == 0 && !output.output.empty())
				sysrootToInject = utility::trim(output.output);
		}

		if (!sysrootToInject.empty())
		{
			bool hasSysroot = false;
			for (const std::string& flag: commandLine)
			{
				if (utility::isPrefix("-isysroot", flag) || utility::isPrefix("--sysroot", flag))
				{
					hasSysroot = true;
					break;
				}
			}
			if (!hasSysroot)
			{
				commandLine.push_back("-isysroot");
				commandLine.push_back(sysrootToInject);
			}
		}

		// Inject C++20 module dependency flags (hybrid: .modmap if available, else fallback).
		if (!entry.sourceDir.empty())
			utility::append(commandLine, getModuleFlags(entry, buildDir));

		// Append user-specified extra flags from settings.
		utility::append(commandLine, m_settings->getCompilerFlags());

		// Source file itself.
		commandLine.push_back(entry.path.str());

		const std::string compilerPath = entry.compileGroup ? entry.compileGroup->compilerPath : "";
		LOG_INFO(
			"indexer command: " + entry.path.fileName() +
			" compiler='" + compilerPath +
			"' flags=" + std::to_string(commandLine.size()));
		provider->addCommand(std::make_shared<IndexerCommandCxx>(
			entry.path,
			utility::concat(indexedHeaderPaths, {entry.path}),
			utility::toSet(excludeFilters),
			std::set<FilePathFilter>{},
			buildDir,
			std::move(commandLine),
			compilerPath));
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
