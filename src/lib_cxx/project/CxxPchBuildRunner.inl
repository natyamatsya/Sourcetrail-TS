// Inline implementations for CxxPchBuildRunner.h. Included at the end of that header (classic) or
// via the srctrl.cxx:package wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/raw_ostream.h>

#include "CanonicalFilePathCache.h"
#include "CxxCompilationDatabaseSingle.h"
#include "CxxDiagnosticConsumer.h"
#include "CxxParser.h"
#include "FilePath.h"
#include "FilePathFilter.h"
#include "FileRegister.h"
#include "FileSystem.h"
#include "GeneratePCHAction.h"
#include "IntermediateStorage.h"
#include "IntermediateStorageSerializer.h"
#include "ParserClientImpl.h"
#include "SingleFrontendActionFactory.h"
#include "ToolChain.h"
#include "logging.h"
#include "utility.h"
#endif

inline int CxxPchBuildRunner::run(const FilePath& requestPath)
{
	FilePath pchInputFilePath;
	FilePath pchOutputFilePath;
	std::vector<std::string> compilerFlags;
	std::string compilerPath;
	try
	{
		std::ifstream in(requestPath.str());
		const nlohmann::json request = nlohmann::json::parse(in);
		pchInputFilePath = FilePath(request.value("pchInput", std::string()));
		pchOutputFilePath = FilePath(request.value("pchOutput", std::string()));
		compilerFlags = request.value("compilerFlags", std::vector<std::string>{});
		compilerPath = request.value("compilerPath", std::string());
	}
	catch (const nlohmann::json::exception& e)
	{
		LOG_ERROR(std::string("PCH build request was not valid JSON: ") + e.what());
		return 1;
	}
	if (pchInputFilePath.empty() || pchOutputFilePath.empty())
	{
		LOG_ERROR("PCH build request is missing pchInput/pchOutput");
		return 1;
	}

	// --- verbatim the former in-main PCH build (utilitySourceGroupCxx::createBuildPchTaskForInput) --
	CxxParser::initializeLLVM();

	if (!pchOutputFilePath.getParentDirectory().exists())
	{
		FileSystem::createDirectories(pchOutputFilePath.getParentDirectory());
	}

	std::shared_ptr<IntermediateStorage> storage = std::make_shared<IntermediateStorage>();
	std::shared_ptr<ParserClientImpl> client = std::make_shared<ParserClientImpl>(storage);

	std::shared_ptr<FileRegister> fileRegister = std::make_shared<FileRegister>(
		pchInputFilePath, std::set<FilePath>{pchInputFilePath}, std::set<FilePathFilter>{});

	std::shared_ptr<CanonicalFilePathCache> canonicalFilePathCache =
		std::make_shared<CanonicalFilePathCache>(fileRegister);

	clang::tooling::CompileCommand pchCommand;
	pchCommand.Filename = pchInputFilePath.fileName();
	pchCommand.Directory = pchOutputFilePath.getParentDirectory().str();
	pchCommand.CommandLine = utility::concat(
		std::vector<std::string>{ClangCompiler::TOOL_NAME},
		CxxParser::getCommandlineArgumentsEssential(compilerPath, compilerFlags));

	CxxCompilationDatabaseSingle compilationDatabase(pchCommand);
	clang::tooling::ClangTool tool(compilationDatabase, {pchInputFilePath.str()});
	GeneratePCHAction* action = new GeneratePCHAction(*client, *canonicalFilePathCache);

	auto options = std::make_shared<clang::DiagnosticOptions>();
	options->ShowCarets = false;
	options->ShowFixits = false;
	options->ShowSourceRanges = false;
	options->SnippetLineLimit = 0;
	CxxDiagnosticConsumer diagnostics(
		llvm::errs(), options, *client, *canonicalFilePathCache, pchInputFilePath, true);

	tool.setDiagnosticConsumer(&diagnostics);
	tool.clearArgumentsAdjusters();
	// Stack-allocated factory so it isn't leaked (ClangTool::run does not take ownership).
	SingleFrontendActionFactory factory(action);
	tool.run(&factory);

	// --- hand the indexed symbols back to the main process via the filesystem ----------------------
	const flatbuffers::DetachedBuffer buffer = IpcSerializer::serializeIntermediateStorage(*storage);
	std::ofstream storageOut(pchOutputFilePath.str() + ".storage", std::ios::binary);
	storageOut.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));

	return 0;
}
