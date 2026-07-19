#include "CxxModulePrebuilder.h"

#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>

#include "CanonicalFilePathCache.h"
#include "CxxCompilationDatabaseSingle.h"
#include "FilePathFilter.h"
#include "CxxParser.h"
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
// Directory holding the clang / clang-scan-deps binaries: SOURCETRAIL_CLANG_BINDIR if set, else
// empty (rely on PATH, as the codebase already does for `xcrun`).
std::string clangToolDir()
{
	if (const char* dir = std::getenv("SOURCETRAIL_CLANG_BINDIR"))
	{
		return std::string(dir);
	}
	return {};
}

std::string toolPath(const std::string& name)
{
	const std::string dir = clangToolDir();
	return dir.empty() ? name : dir + "/" + name;
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

// Cheap pre-filter: does the file's text mention a module/import at all? Avoids spawning
// clang-scan-deps for the common case of a source group with no C++20 modules. False positives
// (the word appears in a comment/string) only cost a scan that finds nothing; a real module unit
// always contains "module" or "import", so there are no false negatives.
bool mightUseModules(const FilePath& file)
{
	std::ifstream in(file.str());
	if (!in)
	{
		return false;
	}
	std::string line;
	while (std::getline(in, line))
	{
		if (line.find("module") != std::string::npos || line.find("import") != std::string::npos)
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

CxxModulePrebuilder::Result CxxModulePrebuilder::prebuild(
	const std::set<FilePath>& sourceFiles,
	const std::vector<std::string>& baseCompilerFlags,
	const FilePath& cacheDir)
{
	Result result;

	const std::string clangxx = toolPath("clang++");
	const std::string scandeps = toolPath("clang-scan-deps");

	// Flags common to scanning and BMI building: the project's own flags plus a C++20+ standard.
	std::vector<std::string> buildFlags = baseCompilerFlags;
	if (!hasStdFlag(buildFlags))
	{
		buildFlags.push_back("-std=c++20");
	}

	// --- 1. scan every file for the module it provides / requires ---------------------------------
	std::map<std::string, FilePath> moduleProviderFile;	  // logical-name -> interface unit
	std::map<FilePath, std::string> providedModule;		  // interface unit -> logical-name
	std::map<FilePath, std::set<std::string>> fileRequires;

	// Only files that even mention a module/import are worth scanning; a source group with none
	// short-circuits here without spawning a single clang-scan-deps process.
	std::set<FilePath> candidates;
	for (const FilePath& file: sourceFiles)
	{
		if (mightUseModules(file))
		{
			candidates.insert(file);
		}
	}
	if (candidates.empty())
	{
		return result;
	}

	for (const FilePath& file: candidates)
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

	if (moduleProviderFile.empty())
	{
		return result;	  // no module-interface units in this source group
	}
	result.anyModules = true;

	// --- 2. topologically order the interface units by their module dependencies (Kahn's) ---------
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

	// --- 3. build each BMI into the cache, in dependency order ------------------------------------
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

	// --- 4. flags the indexer needs to resolve imports against the cache --------------------------
	// The BMIs were built in-process with the indexer's own libclang configuration, so no target /
	// builtin-header match flags are needed here -- only the path to find them.
	result.sharedFlags = {"-fprebuilt-module-path=" + cacheDir.str()};
	for (const auto& [file, name]: providedModule)
	{
		result.interfaceUnits.insert(file);
	}
	return result;
}
