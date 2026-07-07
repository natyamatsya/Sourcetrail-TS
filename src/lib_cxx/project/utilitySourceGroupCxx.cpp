#include "utilitySourceGroupCxx.h"

#include <fstream>
#include <filesystem>

#include <clang/Tooling/JSONCompilationDatabase.h>

#include "CanonicalFilePathCache.h"
#include "CxxCompilationDatabaseSingle.h"
#include "CxxDiagnosticConsumer.h"
#include "CxxParser.h"
#include "DialogView.h"
#include "FilePathFilter.h"
#include "FileRegister.h"
#include "FileSystem.h"
#include "IndexerCommandCxx.h"
#include "TextAccess.h"

#ifdef emit
#pragma push_macro("emit")
#undef emit
#define SOURCETRAIL_RESTORE_QT_EMIT
#endif

#include "GeneratePCHAction.h"
#include "ParserClientImpl.h"
#include "SingleFrontendActionFactory.h"

#ifdef SOURCETRAIL_RESTORE_QT_EMIT
#pragma pop_macro("emit")
#undef SOURCETRAIL_RESTORE_QT_EMIT
#endif

#include "SourceGroupSettingsWithCxxPchOptions.h"
#include "StorageProvider.h"
#include "TaskLambda.h"
#include "logging.h"
#include "utility.h"
#include "utilityString.h"
#include "ToolChain.h"

using namespace std;
using namespace clang::tooling;

namespace utility
{
namespace
{
// Freshness stamp next to the .pch: the flags hash the PCH was built with. The
// PCH is reused across runs when it is newer than its input header and the flags
// are unchanged. (Like CMake's own PCH, a change to a transitively-included
// header that leaves the top PCH header's mtime untouched is not detected.)
FilePath pchStampPath(const FilePath& pchOutputFilePath)
{
	// Sibling file, not a child path -- getConcatenated appends a path segment.
	return FilePath(pchOutputFilePath.str() + ".stamp");
}

bool isPchFresh(
	const FilePath& pchInputFilePath,
	const FilePath& pchOutputFilePath,
	const std::string& flagsHash)
{
	if (!pchOutputFilePath.exists())
	{
		return false;
	}
	const FilePath stamp = pchStampPath(pchOutputFilePath);
	if (!stamp.exists())
	{
		return false;
	}
	if (FileSystem::getFileInfoForPath(pchInputFilePath).lastWriteTime >
		FileSystem::getFileInfoForPath(pchOutputFilePath).lastWriteTime)
	{
		return false;
	}
	return utility::trim(TextAccess::createFromFile(stamp)->getText()) == flagsHash;
}
}	 // namespace

std::shared_ptr<Task> createBuildPchTaskForInput(
	const FilePath& pchInputFilePath,
	const FilePath& pchOutputFilePath,
	std::vector<std::string> compilerFlags,
	const std::string& compilerPath,
	std::shared_ptr<StorageProvider> storageProvider,
	std::shared_ptr<DialogView> dialogView)
{
	shared_ptr<TaskLambda> pchTask(make_shared<TaskLambda>([]() {}));

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

	// Reuse an up-to-date PCH across runs instead of regenerating unconditionally.
	const std::string flagsHash = IndexerCommandCxx::hashCompilerFlags(compilerFlags);
	if (isPchFresh(pchInputFilePath, pchOutputFilePath, flagsHash))
	{
		LOG_INFO(
			"Reusing up-to-date precompiled header \"" + pchOutputFilePath.str() + "\" for input \"" +
			pchInputFilePath.str() + "\"");
		return pchTask;
	}

	pchTask = std::make_shared<TaskLambda>(
		[dialogView, storageProvider, pchInputFilePath, pchOutputFilePath, compilerFlags, compilerPath, flagsHash]()
	{
		dialogView->showUnknownProgressDialog("Preparing Indexing", "Processing Precompiled Headers");
		LOG_INFO("Generating precompiled header output for input file \"" + pchInputFilePath.str() + "\" at location \"" + pchOutputFilePath.str() + "\"");

		CxxParser::initializeLLVM();

		if (!pchOutputFilePath.getParentDirectory().exists())
		{
			FileSystem::createDirectories(pchOutputFilePath.getParentDirectory());
		}

		std::shared_ptr<IntermediateStorage> storage = std::make_shared<IntermediateStorage>();
		std::shared_ptr<ParserClientImpl> client = std::make_shared<ParserClientImpl>(storage);

		std::shared_ptr<FileRegister> fileRegister = std::make_shared<FileRegister>(pchInputFilePath, std::set<FilePath>{pchInputFilePath}, std::set<FilePathFilter>{});

		std::shared_ptr<CanonicalFilePathCache> canonicalFilePathCache = std::make_shared<CanonicalFilePathCache>(fileRegister);

		clang::tooling::CompileCommand pchCommand;
		pchCommand.Filename = pchInputFilePath.fileName();
		pchCommand.Directory = pchOutputFilePath.getParentDirectory().str();
		// DON'T use "-fsyntax-only" here because it will cause the output file to be erased.
		// Use the real compiler path so the resource dir matches what the translation
		// units get -- otherwise clang rejects the PCH as built with different flags.
		pchCommand.CommandLine = utility::concat({ClangCompiler::TOOL_NAME}, CxxParser::getCommandlineArgumentsEssential(compilerPath, compilerFlags));

		CxxCompilationDatabaseSingle compilationDatabase(pchCommand);
		clang::tooling::ClangTool tool(compilationDatabase, {pchInputFilePath.str()});
		GeneratePCHAction *action = new GeneratePCHAction(client, canonicalFilePathCache); // TODO (petermost): Memory leak?

		auto options = std::make_shared<clang::DiagnosticOptions>();
		options->ShowCarets = false;
		options->ShowFixits = false;
		options->ShowSourceRanges = false;
		options->SnippetLineLimit = 0;
		CxxDiagnosticConsumer diagnostics(
			llvm::errs(), options, client, canonicalFilePathCache, pchInputFilePath, true);

		tool.setDiagnosticConsumer(&diagnostics);
		tool.clearArgumentsAdjusters();
		tool.run(new SingleFrontendActionFactory(action)); // TODO (petermost): Memory leak?

		// Record the freshness stamp only when the PCH was actually produced.
		// Re-check existence freshly -- FilePath::exists() caches, and this path was
		// evaluated as "missing" before generation.
		if (std::filesystem::exists(pchOutputFilePath.str()))
		{
			std::ofstream stampFile(pchStampPath(pchOutputFilePath).str());
			stampFile << flagsHash;
		}

		storageProvider->insert(storage);
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

shared_ptr<CompilationDatabase> loadCDB(const FilePath& cdbPath, std::string* error)
{
	unique_ptr<CompilationDatabase> cdb;
	
	if (cdbPath.empty() || !cdbPath.exists())
		return cdb;

	string errorString;
	cdb = JSONCompilationDatabase::loadFromFile(cdbPath.str(), errorString, JSONCommandLineSyntax::AutoDetect);
	if (cdb == nullptr && error != nullptr)
		*error = errorString;

	if (cdb != nullptr)
		cdb = expandResponseFiles(std::move(cdb), llvm::vfs::getRealFileSystem());

	return cdb;
}

shared_ptr<CompilationDatabase> loadCDB(string_view cdbContent, JSONCommandLineSyntax syntax, string *error)
{
	unique_ptr<CompilationDatabase> cdb;

	if (cdbContent.empty())
		return cdb;
		
	string errorString;
	cdb = JSONCompilationDatabase::loadFromBuffer(cdbContent, errorString, syntax);
	if (cdb == nullptr && error != nullptr)
		*error = errorString;

	return cdb;
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
