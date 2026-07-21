#include "SourceGroupCxxCdb.h"
#ifndef SRCTRL_MODULE_BUILD
#include "IndexerCommand.h"
#endif

#include "Application.h"
#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#include "ClangInvocationInfo.h"
#include "CxxCompilationDatabaseSingle.h"
#endif
#include "CxxIndexerCommandProvider.h"
#include "CxxModulePrebuilder.h"
#ifndef SRCTRL_MODULE_BUILD
#include "IndexerCommandCxx.h"
#include "MessageStatus.h"
#include "SourceGroupSettingsCxxCdb.h"
#endif
#include "TaskLambda.h"
#include "ToolChain.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#endif
#include "utilitySourceGroupCxx.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utilityString.h"
#endif

#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.cxx;
import srctrl.indexer;
import srctrl.messaging;
import srctrl.settings;
import srctrl.utility;
#endif

SourceGroupCxxCdb::SourceGroupCxxCdb(std::shared_ptr<SourceGroupSettingsCxxCdb> settings)
	: m_settings(settings)
{
}

std::expected<void, PrepareIndexingError> SourceGroupCxxCdb::prepareIndexing()
{
	FilePath cdbPath = m_settings->getCompilationDatabasePathExpandedAndAbsolute();
	if (!cdbPath.empty() && !cdbPath.exists())
	{
		std::string error =
			"Can't refresh project. The compilation database of the project does not exist "
			"anymore: " +
			cdbPath.str();
		MessageStatus(error, true).dispatch();
		Application::getInstance()->handleDialog(error, {"Ok"});
		return std::unexpected(PrepareIndexingError::SourcePathMissing);
	}
	return {};
}

std::set<FilePath> SourceGroupCxxCdb::filterToContainedFilePaths(const std::set<FilePath>& filePaths) const
{
	return SourceGroup::filterToContainedFilePaths(
		filePaths,
		getAllSourceFilePaths(),
		utility::toSet(m_settings->getIndexedHeaderPathsExpandedAndAbsolute()),
		m_settings->getExcludeFiltersExpandedAndAbsolute());
}

std::set<FilePath> SourceGroupCxxCdb::getAllSourceFilePaths() const
{
	const auto cdb = utility::loadCDB(m_settings->getCompilationDatabasePathExpandedAndAbsolute());
	return cdb ? getAllSourceFilePaths(cdb.value()) : std::set<FilePath>{};
}

std::set<FilePath> SourceGroupCxxCdb::getAllSourceFilePaths(std::shared_ptr<clang::tooling::CompilationDatabase> cdb) const
{
	std::set<FilePath> sourceFilePaths;

	if (cdb)
	{
		const std::vector<FilePathFilter> excludeFilters =
			m_settings->getExcludeFiltersExpandedAndAbsolute();
		for (const FilePath& path: IndexerCommandCxx::getSourceFilesFromCDB(
				 cdb, m_settings->getCompilationDatabasePathExpandedAndAbsolute()))
		{
			bool excluded = FilePathFilter::areMatching(excludeFilters, path);
			if (!excluded && path.exists())
			{
				sourceFilePaths.insert(path);
			}
		}
	}

	return sourceFilePaths;
}

std::shared_ptr<IndexerCommandProvider> SourceGroupCxxCdb::getIndexerCommandProvider(const RefreshInfo &info) const
{
	std::shared_ptr<CxxIndexerCommandProvider> provider = std::make_shared<CxxIndexerCommandProvider>();

	const FilePath cdbPath = m_settings->getCompilationDatabasePathExpandedAndAbsolute();
	const auto loadedCdb = utility::loadCDB(cdbPath);
	if (!loadedCdb)
	{
		return provider;
	}
	const std::shared_ptr<clang::tooling::CompilationDatabase> cdb = loadedCdb.value();

	std::vector<std::string> compilerFlags = getBaseCompilerFlags();
	utility::append(compilerFlags, m_settings->getCompilerFlags());

	const std::vector<std::string> includePchFlags = utility::getIncludePchFlags(m_settings.get());

	const std::set<FilePath> indexedHeaderPaths = utility::toSet(m_settings->getIndexedHeaderPathsExpandedAndAbsolute());
	const std::set<FilePathFilter> excludeFilters = utility::toSet(m_settings->getExcludeFiltersExpandedAndAbsolute());
	const std::set<FilePath> &sourceFilePaths = getAllSourceFilePaths(cdb);

	// Prepare each TU's final flags first, so the module prebuild can see them (and inject its own
	// flags back) before the IndexerCommandCxx objects are created.
	struct PreparedCommand
	{
		FilePath sourcePath;
		FilePath directory;
		std::vector<std::string> flags;
	};
	std::vector<PreparedCommand> prepared;

	for (const clang::tooling::CompileCommand &command : cdb->getAllCompileCommands())
	{
		FilePath sourcePath = FilePath(command.Filename).makeCanonical();
		if (!sourcePath.isAbsolute())
		{
			sourcePath = FilePath(command.Directory + '/' + command.Filename).makeCanonical();
			if (!sourcePath.isAbsolute())
			{
				sourcePath = cdbPath.getParentDirectory().getConcatenated(sourcePath).makeCanonical();
			}
		}

		if (info.filesToIndex.find(sourcePath) != info.filesToIndex.end() && sourceFilePaths.find(sourcePath) != sourceFilePaths.end())
		{
			std::vector<std::string> commandLine = command.CommandLine;

			utility::removeIncludePchFlag(commandLine);
			replaceMsvcArguments(&commandLine);

			if (command.CommandLine.size() != commandLine.size())
			{
				utility::append(commandLine, includePchFlags);
			}

			// On macOS, inject the SDK sysroot unless the CDB already carries one.
			// Sourcetrail's libclang replaces the real compiler, so the sysroot the
			// driver would find natively (SDK C headers, frameworks, libc++ layering)
			// must be passed explicitly -- without it <filesystem>/<map>/etc. fail.
			utility::append(commandLine, IndexerCommandCxx::getCompilerFlagsForSysroot(command.CommandLine));

			prepared.push_back({sourcePath, FilePath(command.Directory), utility::concat(commandLine, compilerFlags)});
		}
	}

	// C++20 modules: build BMIs for the source group's module-interface units (crash-isolated in a
	// subprocess) unless the compile commands already carry module flags -- a modules-aware build
	// system emits `-fmodule-file=` / `-fprebuilt-module-path=` and resolves imports on its own.
	const auto carriesModuleFlags = [](const std::vector<std::string>& flags) {
		for (const std::string& f: flags)
		{
			if (f.rfind("-fmodule-file", 0) == 0 || f.rfind("-fprebuilt-module-path", 0) == 0)
			{
				return true;
			}
		}
		return false;
	};
	std::map<FilePath, std::vector<std::string>> fileFlags;
	bool cdbHandlesModules = false;
	for (const PreparedCommand& pc: prepared)
	{
		fileFlags[pc.sourcePath] = pc.flags;
		cdbHandlesModules = cdbHandlesModules || carriesModuleFlags(pc.flags);
	}
	CxxModulePrebuilder::Result modules;
	if (!cdbHandlesModules)
	{
		modules = CxxModulePrebuilder::prebuild(
			fileFlags, m_settings->getProjectDirectoryPath().getConcatenated("/module_cache"));
	}

	for (const PreparedCommand& pc: prepared)
	{
		std::vector<std::string> flags = pc.flags;
		if (modules.anyModules)
		{
			utility::append(flags, modules.sharedFlags);
			if (modules.interfaceUnits.find(pc.sourcePath) != modules.interfaceUnits.end())
			{
				utility::append(flags, std::vector<std::string>{"-x", "c++-module"});
			}
		}

		provider->addCommand(std::make_shared<IndexerCommand>(
			pc.sourcePath,
			IndexerCommandCxx(
				pc.sourcePath,
				utility::concat(indexedHeaderPaths, {pc.sourcePath}),
				excludeFilters,
				std::set<FilePathFilter>{},
				pc.directory,
				flags,
				"")));
	}

	provider->logStats();

	return provider;
}

std::vector<std::shared_ptr<IndexerCommand>> SourceGroupCxxCdb::getIndexerCommands(
	const RefreshInfo& info) const
{
	return getIndexerCommandProvider(info)->consumeAllCommands();
}

std::shared_ptr<Task> SourceGroupCxxCdb::getPreIndexTask(
	std::shared_ptr<StorageProvider> storageProvider, std::shared_ptr<DialogView> dialogView) const
{
	if (m_settings->getPchInputFilePath().empty())
	{
		return std::make_shared<TaskLambda>([]() {});
	}

	std::vector<std::string> compilerFlags;

	if (m_settings->getUseCompilerFlags())
	{
		const FilePath cdbPath = m_settings->getCompilationDatabasePathExpandedAndAbsolute();
		if (const auto loadedCdb = utility::loadCDB(cdbPath))
		{
			const std::shared_ptr<clang::tooling::CompilationDatabase> cdb = loadedCdb.value();
			const std::set<FilePath> sourceFilePaths = getAllSourceFilePaths(cdb);
			for (const clang::tooling::CompileCommand& command: cdb->getAllCompileCommands())
			{
				FilePath sourcePath = FilePath(command.Filename).makeCanonical();
				if (!sourcePath.isAbsolute())
				{
					sourcePath = FilePath(command.Directory + '/' + command.Filename).makeCanonical();
					if (!sourcePath.isAbsolute())
					{
						sourcePath =
							cdbPath.getParentDirectory().getConcatenated(sourcePath).makeCanonical();
					}
				}

				if (sourceFilePaths.find(sourcePath) != sourceFilePaths.end() &&
					utility::containsIncludePchFlag(command.CommandLine))
				{
					for (const std::string& arg: command.CommandLine)
					{
						if ((!compilerFlags.empty() || utility::isPrefix("-", arg)) &&
							FilePath(arg).fileName() != sourcePath.fileName())
						{
							compilerFlags.emplace_back(arg);
						}
					}

					CxxCompilationDatabaseSingle compilationDatabase(command);
					ClangInvocationInfo info = ClangInvocationInfo::getClangInvocationString(
						&compilationDatabase);

					if (info.invocation.find("\"" + ClangCompiler::languageOption() + "\" \"" + ClangCompiler::CPP_LANGUAGE + "\"") != 0)
					{
						compilerFlags.push_back(ClangCompiler::languageOption());
						compilerFlags.push_back(ClangCompiler::CPP_LANGUAGE);
					}
					break;
				}
			}
		}
	}

	utility::append(compilerFlags, getBaseCompilerFlags());

	if (m_settings->getUseCompilerFlags())
	{
		utility::append(compilerFlags, m_settings->getCompilerFlags());
	}

	utility::append(compilerFlags, m_settings->getPchFlags());

	return utility::createBuildPchTask(m_settings.get(), compilerFlags, storageProvider, dialogView);
}

std::shared_ptr<SourceGroupSettings> SourceGroupCxxCdb::getSourceGroupSettings()
{
	return m_settings;
}

std::shared_ptr<const SourceGroupSettings> SourceGroupCxxCdb::getSourceGroupSettings() const
{
	return m_settings;
}

std::vector<std::string> SourceGroupCxxCdb::getBaseCompilerFlags() const
{
	std::vector<std::string> compilerFlags;

	std::shared_ptr<ApplicationSettings> appSettings = ApplicationSettings::getInstance();

	utility::append(
		compilerFlags,
		IndexerCommandCxx::getCompilerFlagsForSystemHeaderSearchPaths(utility::concat(
			m_settings->getHeaderSearchPathsExpandedAndAbsolute(),
			appSettings->getHeaderSearchPathsExpanded())));

	utility::append(
		compilerFlags,
		IndexerCommandCxx::getCompilerFlagsForFrameworkSearchPaths(utility::concat(
			m_settings->getFrameworkSearchPathsExpandedAndAbsolute(),
			appSettings->getFrameworkSearchPathsExpanded())));

	return compilerFlags;
}
