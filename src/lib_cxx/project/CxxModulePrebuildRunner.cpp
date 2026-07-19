#include "CxxModulePrebuildRunner.h"

#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>

#include "CanonicalFilePathCache.h"
#include "CxxCompilationDatabaseSingle.h"
#include "CxxParser.h"
#include "FilePath.h"
#include "FilePathFilter.h"
#include "FileRegister.h"
#include "FileSystem.h"
#include "IntermediateStorage.h"
#include "ParserClientImpl.h"
#include "ResourcePaths.h"
#include "SingleFrontendActionFactory.h"
#include "ToolChain.h"
#include "logging.h"
#include "utility.h"
#include "utilityApp.h"
#include "utilityClang.h"

namespace
{
// Directory holding clang-scan-deps: SOURCETRAIL_CLANG_BINDIR if set, else empty (rely on PATH, as
// the codebase already does for `xcrun`). Only the dependency scan still needs an external tool; the
// BMI is built in-process (see buildBmiInProcess).
std::string toolPath(const std::string& name)
{
	if (const char* dir = std::getenv("SOURCETRAIL_CLANG_BINDIR"))
	{
		return std::string(dir) + "/" + name;
	}
	return name;
}

bool hasStdFlag(const std::vector<std::string>& flags)
{
	for (const std::string& f: flags)
	{
		if (f.rfind("-std=", 0) == 0)
		{
			return true;
		}
	}
	return false;
}

// Builds one module-interface BMI in-process via a libTooling FrontendAction (the same machinery as
// the PCH build), so the BMI is created with the exact libclang configuration the indexer later
// loads it with -- no external clang++, and no target/resource-dir mismatch to paper over. Returns
// true if the .pcm was produced.
bool buildBmiInProcess(
	const FilePath& inputFile,
	const FilePath& bmiPath,
	const FilePath& cacheDir,
	const std::vector<std::string>& essentialInputFlags)
{
	CxxParser::initializeLLVM();

	// BMI generation records nothing into Sourcetrail storage; a throwaway client satisfies the API.
	auto storage = std::make_shared<IntermediateStorage>();
	auto client = std::make_shared<ParserClientImpl>(storage);
	auto fileRegister = std::make_shared<FileRegister>(
		inputFile, std::set<FilePath>{inputFile}, std::set<FilePathFilter>{});
	auto cache = std::make_shared<CanonicalFilePathCache>(fileRegister);

	std::vector<std::string> flags = essentialInputFlags;
	utility::append(
		flags,
		std::vector<std::string>{
			"-x",
			"c++-module",
			"-fprebuilt-module-path=" + cacheDir.str(),
			inputFile.str(),
			"-Xclang",
			"-emit-module-interface",
			"-o",
			bmiPath.str()});

	clang::tooling::CompileCommand command;
	command.Filename = inputFile.fileName();
	command.Directory = inputFile.getParentDirectory().str();
	command.CommandLine = utility::concat(
		std::vector<std::string>{ClangCompiler::TOOL_NAME},
		CxxParser::getCommandlineArgumentsEssential(std::string(), flags));

	CxxCompilationDatabaseSingle database(command);
	clang::tooling::ClangTool tool(database, {inputFile.str()});
	tool.clearArgumentsAdjusters();

	// Match the indexer's builtin-header handling for an empty compiler path (see CxxParser::runTool).
	if (!utility::resolveCompilerResourceDir(std::string()))
	{
		tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
			ResourcePaths::getCxxCompilerHeaderDirectoryPath().str().c_str(),
			clang::tooling::ArgumentInsertPosition::BEGIN));
		tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
			ClangCompiler::systemIncludeOption().c_str(),
			clang::tooling::ArgumentInsertPosition::BEGIN));
	}

	tool.run(new SingleFrontendActionFactory(new clang::GenerateModuleInterfaceAction()));
	return bmiPath.recheckExists();
}
}	 // namespace

int CxxModulePrebuildRunner::run(const FilePath& requestPath)
{
	// --- read the request --------------------------------------------------------------------------
	FilePath cacheDir;
	std::set<FilePath> sourceFiles;
	std::vector<std::string> baseFlags;
	try
	{
		std::ifstream in(requestPath.str());
		const nlohmann::json request = nlohmann::json::parse(in);
		cacheDir = FilePath(request.value("cacheDir", std::string()));
		baseFlags = request.value("baseFlags", std::vector<std::string>{});
		for (const std::string& file: request.value("sourceFiles", std::vector<std::string>{}))
		{
			sourceFiles.insert(FilePath(file));
		}
	}
	catch (const nlohmann::json::exception& e)
	{
		LOG_ERROR(std::string("module prebuild request was not valid JSON: ") + e.what());
		return 1;
	}
	if (cacheDir.empty())
	{
		LOG_ERROR("module prebuild request has no cacheDir");
		return 1;
	}

	const std::string scandeps = toolPath("clang-scan-deps");
	const std::string clangxx = toolPath("clang++");

	std::vector<std::string> buildFlags = baseFlags;
	if (!hasStdFlag(buildFlags))
	{
		buildFlags.push_back("-std=c++20");
	}

	// --- 1. scan every file for the module it provides / requires ----------------------------------
	std::map<std::string, FilePath> moduleProviderFile;	  // logical-name -> interface unit
	std::map<FilePath, std::string> providedModule;		  // interface unit -> logical-name
	std::map<FilePath, std::set<std::string>> fileRequires;

	for (const FilePath& file: sourceFiles)
	{
		std::vector<std::string> args = {"-format=p1689", "--", clangxx};
		utility::append(args, buildFlags);
		utility::append(args, {"-c", file.str(), "-o", file.str() + ".o"});

		const utility::ProcessOutput out = utility::executeProcess(scandeps, args, FilePath(), false);
		if (out.exitCode != 0 || out.output.empty())
		{
			continue;	 // not scannable (e.g. tools missing) -> treated as a non-module file
		}

		try
		{
			const nlohmann::json doc = nlohmann::json::parse(out.output);
			for (const auto& rule: doc.value("rules", nlohmann::json::array()))
			{
				for (const auto& provide: rule.value("provides", nlohmann::json::array()))
				{
					const std::string name = provide.value("logical-name", std::string());
					if (!name.empty())
					{
						moduleProviderFile[name] = file;
						providedModule[file] = name;
					}
				}
				for (const auto& require: rule.value("requires", nlohmann::json::array()))
				{
					const std::string name = require.value("logical-name", std::string());
					if (!name.empty())
					{
						fileRequires[file].insert(name);
					}
				}
			}
		}
		catch (const nlohmann::json::exception& e)
		{
			LOG_WARNING(std::string("clang-scan-deps output was not valid JSON: ") + e.what());
		}
	}

	// --- 2. topologically order the interface units by their module dependencies (Kahn's) ----------
	std::map<FilePath, int> indegree;
	std::map<std::string, std::vector<FilePath>> dependents;	  // module -> interface units needing it
	for (const auto& [file, name]: providedModule)
	{
		indegree.try_emplace(file, 0);
	}
	for (const auto& [file, name]: providedModule)
	{
		for (const std::string& required: fileRequires[file])
		{
			auto providerIt = moduleProviderFile.find(required);
			if (providerIt != moduleProviderFile.end() && providerIt->second != file)
			{
				dependents[required].push_back(file);
				indegree[file]++;
			}
		}
	}

	std::deque<FilePath> ready;
	for (const auto& [file, degree]: indegree)
	{
		if (degree == 0)
		{
			ready.push_back(file);
		}
	}

	std::vector<FilePath> buildOrder;
	while (!ready.empty())
	{
		const FilePath file = ready.front();
		ready.pop_front();
		buildOrder.push_back(file);
		for (const FilePath& dependent: dependents[providedModule[file]])
		{
			if (--indegree[dependent] == 0)
			{
				ready.push_back(dependent);
			}
		}
	}

	if (buildOrder.size() != providedModule.size())
	{
		LOG_ERROR(
			"C++20 module dependency cycle detected; some BMIs will not be built and imports into "
			"the cycle will not resolve.");
	}

	// --- 3. build each BMI into the cache, in dependency order -------------------------------------
	FileSystem::createDirectories(cacheDir);
	for (const FilePath& file: buildOrder)
	{
		const std::string logicalName = providedModule[file];
		const FilePath bmiPath = cacheDir.getConcatenated("/" + logicalName + ".pcm");
		if (!buildBmiInProcess(file, bmiPath, cacheDir, buildFlags))
		{
			LOG_ERROR("Failed to build BMI for module '" + logicalName + "' (" + file.str() + ")");
		}
	}

	// --- 4. write the manifest the main process reads back -----------------------------------------
	nlohmann::json manifest;
	manifest["interfaceUnits"] = nlohmann::json::array();
	for (const auto& [file, name]: providedModule)
	{
		manifest["interfaceUnits"].push_back(file.str());
	}
	std::ofstream manifestOut(cacheDir.getConcatenated("/manifest.json").str());
	manifestOut << manifest.dump();

	return 0;
}
