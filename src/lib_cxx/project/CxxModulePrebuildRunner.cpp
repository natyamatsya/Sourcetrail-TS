#include "CxxModulePrebuildRunner.h"

#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <clang/Basic/LangOptions.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/Lexer.h>
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
struct ModuleScan
{
	std::string provides;			  // the module this file's `export module` declares, if any
	std::set<std::string> requires_;  // the modules this file `import`s
};

// Parse a (possibly dotted / partitioned) module name starting at token `i`: identifier, then any
// run of `.ident` (submodules) or `:ident` (partition). Leaves `i` past the name.
std::string parseModuleName(const std::vector<clang::Token>& toks, size_t& i)
{
	if (i >= toks.size() || !toks[i].is(clang::tok::raw_identifier))
	{
		return {};
	}
	std::string name = toks[i].getRawIdentifier().str();
	++i;
	while (i + 1 < toks.size() &&
		   (toks[i].is(clang::tok::period) || toks[i].is(clang::tok::colon)) &&
		   toks[i + 1].is(clang::tok::raw_identifier))
	{
		name += (toks[i].is(clang::tok::colon) ? ":" : ".") + toks[i + 1].getRawIdentifier().str();
		i += 2;
	}
	return name;
}

// In-process replacement for `clang-scan-deps -format=p1689`: raw-lex the file (no preprocessing)
// and pull the module declaration and imports out of the token stream. Robust to comments/strings
// (the lexer handles them); the known gap vs the real scanner is module decls hidden behind macros
// or #if, which is rare. `export module X` provides X; `import X` / `export import X` requires X;
// header-unit imports (`import <h>` / `import "h"`) are skipped -- they aren't source-group units.
ModuleScan scanModuleDeps(const FilePath& file)
{
	ModuleScan scan;
	std::ifstream in(file.str(), std::ios::binary);
	if (!in)
	{
		return scan;
	}
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

	clang::LangOptions opts;
	opts.CPlusPlus = true;
	opts.CPlusPlus20 = true;
	opts.CPlusPlusModules = true;
	clang::Lexer lexer(
		clang::SourceLocation(), opts, text.data(), text.data(), text.data() + text.size());

	std::vector<clang::Token> toks;
	clang::Token tok;
	while (true)
	{
		lexer.LexFromRawLexer(tok);
		if (tok.is(clang::tok::eof))
		{
			break;
		}
		toks.push_back(tok);
	}

	const auto isId = [](const clang::Token& t, const char* s) {
		return t.is(clang::tok::raw_identifier) && t.getRawIdentifier() == s;
	};

	for (size_t i = 0; i < toks.size(); ++i)
	{
		if (isId(toks[i], "export") && i + 1 < toks.size())
		{
			if (isId(toks[i + 1], "module"))
			{
				size_t j = i + 2;
				if (std::string name = parseModuleName(toks, j); !name.empty())
				{
					scan.provides = name;
				}
			}
			else if (isId(toks[i + 1], "import"))
			{
				size_t j = i + 2;
				if (std::string name = parseModuleName(toks, j); !name.empty())
				{
					scan.requires_.insert(name);
				}
			}
		}
		else if (isId(toks[i], "import") && i + 1 < toks.size() && toks[i + 1].is(clang::tok::raw_identifier))
		{
			size_t j = i + 1;
			if (std::string name = parseModuleName(toks, j); !name.empty())
			{
				scan.requires_.insert(name);
			}
		}
	}
	return scan;
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
		const ModuleScan scan = scanModuleDeps(file);
		if (!scan.provides.empty())
		{
			moduleProviderFile[scan.provides] = file;
			providedModule[file] = scan.provides;
		}
		for (const std::string& required: scan.requires_)
		{
			fileRequires[file].insert(required);
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
