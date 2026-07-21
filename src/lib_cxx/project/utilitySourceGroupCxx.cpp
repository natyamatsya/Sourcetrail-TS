#include "utilitySourceGroupCxx.h"

#include <fstream>
#include <filesystem>

#include <nlohmann/json.hpp>

#include <clang/Tooling/JSONCompilationDatabase.h>

#ifndef SRCTRL_MODULE_BUILD
#include "AppPath.h"
#include "CanonicalFilePathCache.h"
#include "IntermediateStorageSerializer.h"
#include "UserPaths.h"
#include "utilityApp.h"
#include "CxxCompilationDatabaseSingle.h"
#include "CxxDiagnosticConsumer.h"
#include "CxxParser.h"
#endif
#include "DialogView.h"
#ifndef SRCTRL_MODULE_BUILD
#include "FilePathFilter.h"
#include "FileRegister.h"
#include "FileSystem.h"
#include "IndexerCommandCxx.h"
#include "TextAccess.h"
#endif

#ifndef SRCTRL_MODULE_BUILD
#include "GeneratePCHAction.h"
#include "ParserClientImpl.h"
#include "SingleFrontendActionFactory.h"
#endif

#include "SourceGroupSettingsWithCxxPchOptions.h"
#include "StorageProvider.h"
#include "TaskLambda.h"
#include "logging.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#include "utilityString.h"
#endif
#include "ToolChain.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.cxx;
import srctrl.file;
import srctrl.indexer;
import srctrl.interprocess;
import srctrl.process;
import srctrl.utility;
#endif


namespace utility
{

std::expected<void, IndexerPrebuildError> runIndexerPrebuildMode(const std::string& modeArgument)
{
	const FilePath indexerPath = AppPath::getCxxIndexerFilePath();
	if (!indexerPath.exists())
	{
		LOG_ERROR("Cannot run indexer prebuild: executable missing at " + indexerPath.str());
		return std::unexpected(IndexerPrebuildError::IndexerExecutableMissing);
	}

	// Positionals mirror a normal indexer launch (processId, uuid, sharedDataPath, userDataPath); a
	// prebuild ignores the IPC ones but needs the data paths for resource resolution.
	const std::vector<std::string> args = {
		"0",
		"prebuild",
		AppPath::getSharedDataDirectoryPath().getAbsolute().str(),
		UserPaths::getUserDataDirectoryPath().getAbsolute().str(),
		modeArgument};

	const int exitCode =
		utility::executeProcess(indexerPath.str(), args, FilePath(), false, utility::INFINITE_TIMEOUT)
			.exitCode;
	if (exitCode != 0)
	{
		LOG_ERROR("Indexer prebuild subprocess exited " + std::to_string(exitCode));
		return std::unexpected(IndexerPrebuildError::SubprocessFailed);
	}
	return {};
}

std::shared_ptr<Task> createBuildPchTaskForInput(
	const FilePath& pchInputFilePath,
	const FilePath& pchOutputFilePath,
	std::vector<std::string> compilerFlags,
	const std::string& compilerPath,
	std::shared_ptr<StorageProvider> storageProvider,
	std::shared_ptr<DialogView> dialogView)
{
	std::shared_ptr<TaskLambda> pchTask(std::make_shared<TaskLambda>([]() {}));

	if (pchInputFilePath.empty() || pchOutputFilePath.empty())
	{
		return pchTask;
	}
	if (!pchInputFilePath.exists())
	{
		LOG_ERROR("Precompiled header input file \"" + pchInputFilePath.str() + "\" does not exist.");
		return pchTask;
	}

	utility::removeIncludePchFlag(compilerFlags);
	compilerFlags.push_back(pchInputFilePath.str());
	// -emit-pch is a cc1 flag: pass it through -Xclang so the driver accepts it.
	compilerFlags.push_back("-Xclang");
	compilerFlags.push_back(ClangCompiler::emitPchOption());
	compilerFlags.push_back(ClangCompiler::outputOption());
	compilerFlags.push_back(pchOutputFilePath.str());

	// Always (re)build and index the PCH -- never short-circuit on a freshness
	// check. Building indexes the header's symbols into storageProvider exactly once
	// (translation units skip re-recording PCH-loaded declarations), so reusing a
	// prebuilt PCH would drop every precompiled-header symbol whenever the database
	// was rebuilt from empty: a full refresh, or an incremental refresh that
	// re-indexes all of the header's dependents (which orphans and clears it).
	pchTask = std::make_shared<TaskLambda>(
		[dialogView, storageProvider, pchInputFilePath, pchOutputFilePath, compilerFlags, compilerPath]()
	{
		dialogView->showUnknownProgressDialog("Preparing Indexing", "Processing Precompiled Headers");
		LOG_INFO("Generating precompiled header output for input file \"" + pchInputFilePath.str() + "\" at location \"" + pchOutputFilePath.str() + "\"");

		if (!pchOutputFilePath.getParentDirectory().exists())
		{
			FileSystem::createDirectories(pchOutputFilePath.getParentDirectory());
		}

		// Build the PCH in a crash-isolated sourcetrail_indexer subprocess (--prebuild-pch): it
		// parses arbitrary user code, so it belongs off the main process. It writes the .pch and
		// serializes the symbols it indexes to <pchOutput>.storage for us to merge back.
		const FilePath requestPath = pchOutputFilePath.getParentDirectory().getConcatenated("/pch_request.json");
		const FilePath storagePath = FilePath(pchOutputFilePath.str() + ".storage");
		FileSystem::remove(storagePath);

		nlohmann::json request;
		request["pchInput"] = pchInputFilePath.str();
		request["pchOutput"] = pchOutputFilePath.str();
		request["compilerFlags"] = compilerFlags;
		request["compilerPath"] = compilerPath;
		{
			std::ofstream out(requestPath.str());
			out << request.dump();
		}

		const std::expected<void, IndexerPrebuildError> prebuilt =
			runIndexerPrebuildMode("--prebuild-pch=" + requestPath.str());
		if (!prebuilt || !storagePath.recheckExists())
		{
			LOG_ERROR(
				std::string("PCH build failed (") +
				(prebuilt ? "storage missing" : std::string(to_std_sv(prebuilt.error()))) +
				"); the precompiled header and its symbols are missing.");
			return;
		}

		std::ifstream in(storagePath.str(), std::ios::binary);
		const std::vector<uint8_t> bytes(
			(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (std::shared_ptr<IntermediateStorage> storage =
				IpcSerializer::deserializeIntermediateStorage(bytes.data(), bytes.size()))
		{
			storageProvider->insert(storage);
		}
	});
	return pchTask;
}

std::shared_ptr<Task> createBuildPchTask(const SourceGroupSettingsWithCxxPchOptions *settings, std::vector<std::string> compilerFlags,
	std::shared_ptr<StorageProvider> storageProvider, std::shared_ptr<DialogView> dialogView)
{
	const FilePath pchInputFilePath = settings->getPchInputFilePathExpandedAndAbsolute();
	const FilePath pchDependenciesDirectoryPath = settings->getPchDependenciesDirectoryPath();

	if (pchInputFilePath.empty() || pchDependenciesDirectoryPath.empty())
	{
		return std::make_shared<TaskLambda>([]() {});
	}

	const FilePath pchOutputFilePath =
		pchDependenciesDirectoryPath.getConcatenated(pchInputFilePath.fileName()).replaceExtension("pch");

	return createBuildPchTaskForInput(
		pchInputFilePath, pchOutputFilePath, std::move(compilerFlags), "", storageProvider, dialogView);
}

bool containsIncludePchFlags(std::shared_ptr<clang::tooling::CompilationDatabase> cdb)
{
	for (const clang::tooling::CompileCommand& command: cdb->getAllCompileCommands())
	{
		if (containsIncludePchFlag(command.CommandLine))
		{
			return true;
		}
	}
	return false;
}

bool containsIncludePchFlag(const std::vector<std::string>& args)
{
	const std::string includePchPrefix = ClangCompiler::includePchOption();
	for (size_t i = 0; i < args.size(); i++)
	{
		const std::string arg = utility::trim(args[i]);
		if (utility::isPrefix(includePchPrefix, arg))
		{
			return true;
		}
	}
	return false;
}

std::vector<std::string> getWithRemoveIncludePchFlag(const std::vector<std::string>& args)
{
	std::vector<std::string> ret = args;
	removeIncludePchFlag(ret);
	return ret;
}

void removeIncludePchFlag(std::vector<std::string>& args)
{
	const std::string includePchPrefix = ClangCompiler::includePchOption();
	for (size_t i = 0; i < args.size(); i++)
	{
		const std::string arg = utility::trim(args[i]);
		if (utility::isPrefix(includePchPrefix, arg))
		{
			if (i + 1 < args.size() &&
				!utility::isPrefix("-", utility::trim(args[i + 1])) &&
				arg == includePchPrefix)
			{
				args.erase(args.begin() + i + 1);
			}
			args.erase(args.begin() + i);
			i--;
		}
	}
}

std::vector<std::string> getIncludePchFlagsForOutput(const FilePath& pchOutputFilePath)
{
	if (pchOutputFilePath.empty())
	{
		return {};
	}
	// -fallow-pch-with-compiler-errors is a cc1 flag (pass via -Xclang);
	// -include-pch is a driver flag and takes the path as its argument.
	return {"-Xclang",
			ClangCompiler::allowPchWithCompilerErrors(),
			ClangCompiler::includePchOption(),
			pchOutputFilePath.str()};
}

std::vector<std::string> getIncludePchFlags(const SourceGroupSettingsWithCxxPchOptions* settings)
{
	const FilePath pchInputFilePath = settings->getPchInputFilePathExpandedAndAbsolute();
	const FilePath pchDependenciesDirectoryPath = settings->getPchDependenciesDirectoryPath();

	if (!pchInputFilePath.empty() && !pchDependenciesDirectoryPath.empty())
	{
		const FilePath pchOutputFilePath = pchDependenciesDirectoryPath
											   .getConcatenated(pchInputFilePath.fileName())
											   .replaceExtension("pch");

		return getIncludePchFlagsForOutput(pchOutputFilePath);
	}

	return {};
}

}	 // namespace utility
