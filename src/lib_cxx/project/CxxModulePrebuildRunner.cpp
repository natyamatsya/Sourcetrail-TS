#include "CxxModulePrebuildRunner.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/Support/raw_ostream.h>

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

// Parse a (possibly dotted / partitioned) module name starting at token `i`: an optional leading
// identifier, then any run of `.ident` (submodule) or `:ident` (partition). A leading `:` (as in
// `import :part;`) yields a partition-only suffix (":part") which the caller resolves against the
// current module. Leaves `i` past the name.
std::string parseModuleName(const std::vector<clang::Token>& toks, size_t& i)
{
	std::string name;
	if (i < toks.size() && toks[i].is(clang::tok::raw_identifier))
	{
		name = toks[i].getRawIdentifier().str();
		++i;
	}
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
// (the lexer handles them). Known gaps vs the real scanner, all rare: module decls hidden behind
// macros / #if are missed, and a decl inside `#if 0` is a false positive (the raw lexer doesn't
// evaluate the preprocessor). `export module X` provides X; `import X` / `export import X` /
// `import :part` requires X; header-unit imports (`import <h>` / `import "h"`) are skipped -- they
// aren't source-group units.
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

	const auto recordRequire = [&](size_t start) {
		size_t j = start;
		std::string name = parseModuleName(toks, j);
		if (name.rfind(':', 0) == 0)
		{
			// `import :part;` -- a partition of the module this file defines. Resolve it against the
			// current module's base name (empty provides means it's not a module unit -> ignore).
			if (scan.provides.empty())
			{
				return;
			}
			name = scan.provides.substr(0, scan.provides.find(':')) + name;
		}
		if (!name.empty())
		{
			scan.requires_.insert(name);
		}
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
				recordRequire(i + 2);
			}
		}
		else if (
			isId(toks[i], "import") && i + 1 < toks.size() &&
			(toks[i + 1].is(clang::tok::raw_identifier) || toks[i + 1].is(clang::tok::colon)))
		{
			recordRequire(i + 1);
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
// loads it with -- no external clang++, and no target/resource-dir mismatch to paper over. On
// success returns {}; on failure returns the captured clang diagnostics (the real cause -- a missing
// header, an unresolved import, a syntax error) so the caller can surface it instead of a bare
// "build failed".
std::expected<void, std::string> buildBmiInProcess(
	const FilePath& inputFile,
	const FilePath& bmiPath,
	const FilePath& cacheDir,
	const std::vector<std::string>& essentialInputFlags,
	const FilePath& compatShimPath)
{
	CxxParser::initializeLLVM();

	// BMI generation records nothing into Sourcetrail storage; a throwaway client satisfies the API.
	auto storage = std::make_shared<IntermediateStorage>();
	auto client = std::make_shared<ParserClientImpl>(storage);
	auto fileRegister = std::make_shared<FileRegister>(
		inputFile, std::set<FilePath>{inputFile}, std::set<FilePathFilter>{});
	auto cache = std::make_shared<CanonicalFilePathCache>(fileRegister);

	// Normalize the file's flags into clean options: a compile-database command carries the compiler
	// executable (argv[0]), -c, -o <out>, -x <lang> and the input file -- all of which we set
	// ourselves for BMI generation. (The plain C++ Source Group passes clean options, so nothing is
	// stripped there.)
	std::vector<std::string> flags;
	for (size_t i = 0; i < essentialInputFlags.size(); ++i)
	{
		const std::string& f = essentialInputFlags[i];
		if (i == 0 && !f.empty() && f[0] != '-')
		{
			continue;	 // argv[0] (compiler executable) in a CDB command
		}
		if (f == "-c")
		{
			continue;
		}
		if (f == "-o" || f == "-x")
		{
			++i;	 // space-separated form: drop the flag and its argument
			continue;
		}
		if (f.rfind("-o", 0) == 0 || f.rfind("-x", 0) == 0)
		{
			continue;	 // glued form: -o<out> / -x<lang>
		}
		// The input file: a CDB command line may spell it relatively (e.g. `geo.cpp`) while inputFile
		// is canonical, so match on the file name too, not just the exact string.
		if (f == inputFile.str() || FilePath(f).fileName() == inputFile.fileName())
		{
			continue;
		}
		flags.push_back(f);
	}
	utility::append(
		flags,
		std::vector<std::string>{
			"-include",
			compatShimPath.str(),
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

	// Capture clang's diagnostics into a string rather than letting them go to this subprocess's
	// stderr (which the app discards when it spawns the prebuild). Declared so the printer is destroyed
	// before the stream it writes to.
	std::string diagnostics;
	llvm::raw_string_ostream diagnosticStream(diagnostics);
	const std::shared_ptr<clang::DiagnosticOptions> diagnosticOptions =
		std::make_shared<clang::DiagnosticOptions>();
	clang::TextDiagnosticPrinter diagnosticPrinter(diagnosticStream, *diagnosticOptions);
	tool.setDiagnosticConsumer(&diagnosticPrinter);

	// Stack-allocated factory so it isn't leaked (ClangTool::run does not take ownership); the action
	// is adopted into a unique_ptr by SingleFrontendActionFactory::create().
	SingleFrontendActionFactory factory(new clang::GenerateModuleInterfaceAction());
	tool.run(&factory);
	diagnosticStream.flush();

	if (!bmiPath.recheckExists())
	{
		return std::unexpected(
			diagnostics.empty() ? std::string("the compiler produced no .pcm and no diagnostics")
								 : std::move(diagnostics));
	}
	return {};
}

// Locate the toolchain's standard library module source (std.cppm / std.compat.cppm). It ships next
// to libc++ at <prefix>/share/libc++/v1/. We anchor on the LLVM install prefix the app is linked
// against (baked in at build time) so the std.cppm we compile matches the in-process libclang's
// libc++ exactly; as a fallback we derive the prefix from a resolvable compiler's resource dir
// (<prefix>/lib/clang/<v>).
std::optional<FilePath> findStdModuleSource(const std::string& fileName)
{
	std::vector<std::filesystem::path> prefixes;
#ifdef SOURCETRAIL_LLVM_INSTALL_PREFIX
	prefixes.emplace_back(SOURCETRAIL_LLVM_INSTALL_PREFIX);
#endif
	if (const std::optional<std::filesystem::path> resourceDir =
			utility::resolveCompilerResourceDir(std::string()))
	{
		prefixes.push_back(resourceDir->parent_path().parent_path().parent_path());
	}
	for (const std::filesystem::path& prefix: prefixes)
	{
		const std::filesystem::path candidate = prefix / "share" / "libc++" / "v1" / fileName;
		if (std::filesystem::exists(candidate))
		{
			return FilePath(candidate.string());
		}
	}
	return std::nullopt;
}

// A toolchain-compatibility prefix header, -include'd into every module BMI build. Xcode 27's SDK
// cleanup dropped the INFINITY / NAN / HUGE_VAL* C math macros from the headers that <complex>/<cmath>
// pull in, so a module whose global fragment includes them fails with "use of undeclared identifier
// 'INFINITY'" (std.cppm was the first casualty). We re-define the standard builtins if absent.
//
// Only *macro* definitions belong here: a `-include` header is injected before the file's `module;`
// global-module-fragment marker, and a real declaration (e.g. `#include <cstdlib>` to make `getenv`
// visible) there is ill-formed ("'module;' can appear only at the start of the translation unit").
// Macros are exempt because they aren't declarations. Declaration-class gaps (e.g. Xcode 27's `getenv`
// in Vulkan-Hpp) therefore can't be fixed here; they must be resolved in the module's own global
// fragment -- the diagnostics pipeline surfaces them for that. Returns the shim path.
FilePath writeModuleCompatShim(const FilePath& cacheDir)
{
	const FilePath shimPath = cacheDir.getConcatenated("/module_compat_shim.h");
	std::ofstream out(shimPath.str());
	out << "#pragma once\n"
		   "#ifndef INFINITY\n#define INFINITY (__builtin_inff())\n#endif\n"
		   "#ifndef NAN\n#define NAN (__builtin_nanf(\"\"))\n#endif\n"
		   "#ifndef HUGE_VAL\n#define HUGE_VAL (__builtin_huge_val())\n#endif\n"
		   "#ifndef HUGE_VALF\n#define HUGE_VALF (__builtin_huge_valf())\n#endif\n"
		   "#ifndef HUGE_VALL\n#define HUGE_VALL (__builtin_huge_vall())\n#endif\n";
	return shimPath;
}

// Build a standard library module BMI (std / std.compat) into the cache so `import std;` resolves for
// a source group that has no in-group provider for it. Reuses the same in-process FrontendAction as
// source modules. The build is based on the flags of a file that imports std (`baseFlags`) so it
// inherits the group's sysroot and libc++ include paths -- without them std.cppm can't find libc++'s
// internal headers ('__config' not found). Adds the reserved-identifier suppression and the
// reserved-identifier suppression std.cppm needs, and a C++23 -std if the base flags carry none. The
// math-macro compat shim is applied centrally by buildBmiInProcess. std.compat imports std, so std
// must be built first (buildBmiInProcess puts the cache on the module path). On success returns {}; on
// failure returns a message (a missing std.cppm, or the captured clang diagnostics).
std::expected<void, std::string> buildStdModuleBmi(
	const std::string& moduleName,
	const std::string& sourceFileName,
	const FilePath& cacheDir,
	const FilePath& compatShimPath,
	const std::vector<std::string>& baseFlags)
{
	const std::optional<FilePath> source = findStdModuleSource(sourceFileName);
	if (!source)
	{
		return std::unexpected(
			"the toolchain's " + sourceFileName + " was not found (looked next to libc++)");
	}
	const FilePath bmiPath = cacheDir.getConcatenated("/" + moduleName + ".pcm");
	std::vector<std::string> flags = baseFlags;
	if (!hasStdFlag(flags))
	{
		flags.push_back("-std=c++2b");
	}
	flags.push_back("-Wno-reserved-module-identifier");
	return buildBmiInProcess(*source, bmiPath, cacheDir, flags, compatShimPath);
}
}	 // namespace

int CxxModulePrebuildRunner::run(const FilePath& requestPath)
{
	// --- read the request --------------------------------------------------------------------------
	FilePath cacheDir;
	std::set<FilePath> sourceFiles;
	std::map<FilePath, std::vector<std::string>> fileFlags;	  // each file's own compiler flags
	try
	{
		std::ifstream in(requestPath.str());
		const nlohmann::json request = nlohmann::json::parse(in);
		cacheDir = FilePath(request.value("cacheDir", std::string()));
		for (const auto& entry: request.value("files", nlohmann::json::array()))
		{
			const FilePath file(entry.value("path", std::string()));
			sourceFiles.insert(file);
			fileFlags[file] = entry.value("flags", std::vector<std::string>{});
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

	// --- 3. build each BMI into the cache, in dependency order (with the file's own flags) ---------
	FileSystem::createDirectories(cacheDir);
	// Purge stale BMIs so a renamed/deleted module can't leave an orphan .pcm that still resolves on
	// the next refresh.
	for (const auto& entry: std::filesystem::directory_iterator(cacheDir.str()))
	{
		if (entry.path().extension() == ".pcm")
		{
			std::filesystem::remove(entry.path());
		}
	}

	// A single toolchain-compat shim -include'd into *every* BMI build (std and source modules alike):
	// it re-defines the C math macros Xcode 27's SDK cleanup dropped. Safe to force everywhere -- it's
	// macro-only (legal before a `module;` global fragment, unlike a header include) and #ifndef-guarded.
	const FilePath compatShim = writeModuleCompatShim(cacheDir);

	// Per-module BMI build failures, captured with the real clang diagnostics. This subprocess has no
	// logger of its own, so a failure is (a) printed here -- visible when the prebuild is run directly
	// -- and (b) handed back to the main process via the manifest, which logs it into the index run.
	std::vector<nlohmann::json> failures;
	const auto recordFailure =
		[&failures](const std::string& module, const std::string& file, const std::string& diagnostics) {
			std::cerr << "[module-prebuild] failed to build BMI for '" << module << "'"
					  << (file.empty() ? "" : " (" + file + ")") << ":\n"
					  << diagnostics << std::endl;
			failures.push_back({{"module", module}, {"file", file}, {"diagnostics", diagnostics}});
		};

	// 3a. Build the standard library module(s) if any file imports `std` / `std.compat` but no source
	// unit in the group provides them -- the module lives in the toolchain, not the source group, so we
	// compile its shipped std.cppm on demand. Done before the source BMIs so a source module that
	// itself imports std resolves against the freshly built std.pcm.
	std::set<std::string> externalRequires;
	for (const auto& [file, reqs]: fileRequires)
	{
		for (const std::string& required: reqs)
		{
			if (moduleProviderFile.find(required) == moduleProviderFile.end())
			{
				externalRequires.insert(required);
			}
		}
	}
	const bool needStdCompat = externalRequires.count("std.compat") != 0;
	if (externalRequires.count("std") != 0 || needStdCompat)
	{
		// Base the std build on a file that imports std: those flags carry the group's sysroot and
		// libc++ include paths, which std.cppm needs to find its own internal headers.
		std::vector<std::string> baseFlags;
		for (const auto& [file, reqs]: fileRequires)
		{
			if (reqs.count("std") != 0 || reqs.count("std.compat") != 0)
			{
				baseFlags = fileFlags[file];
				break;
			}
		}
		if (const std::expected<void, std::string> built =
				buildStdModuleBmi("std", "std.cppm", cacheDir, compatShim, baseFlags);
			!built)
		{
			recordFailure("std", std::string(), built.error());
		}
		if (needStdCompat)
		{
			if (const std::expected<void, std::string> built =
					buildStdModuleBmi("std.compat", "std.compat.cppm", cacheDir, compatShim, baseFlags);
				!built)
			{
				recordFailure("std.compat", std::string(), built.error());
			}
		}
	}

	// 3b. Build each source-group interface unit's BMI, in dependency order.
	for (const FilePath& file: buildOrder)
	{
		const std::string logicalName = providedModule[file];
		// A partition name carries a ':' -- illegal in a Windows filename -- so sanitize it for the
		// on-disk BMI. (Partition resolution via -fprebuilt-module-path is best-effort; see the
		// design doc.)
		std::string bmiName = logicalName;
		std::replace(bmiName.begin(), bmiName.end(), ':', '-');
		const FilePath bmiPath = cacheDir.getConcatenated("/" + bmiName + ".pcm");
		std::vector<std::string> buildFlags = fileFlags[file];
		if (!hasStdFlag(buildFlags))
		{
			buildFlags.push_back("-std=c++20");
		}
		if (const std::expected<void, std::string> built =
				buildBmiInProcess(file, bmiPath, cacheDir, buildFlags, compatShim);
			!built)
		{
			recordFailure(logicalName, file.str(), built.error());
		}
	}

	if (!failures.empty())
	{
		std::cerr << "[module-prebuild] " << failures.size()
				  << " module BMI build(s) failed; imports of them will not resolve." << std::endl;
	}

	// --- 4. write the manifest the main process reads back -----------------------------------------
	nlohmann::json manifest;
	manifest["interfaceUnits"] = nlohmann::json::array();
	for (const auto& [file, name]: providedModule)
	{
		manifest["interfaceUnits"].push_back(file.str());
	}
	manifest["failures"] = failures;
	std::ofstream manifestOut(cacheDir.getConcatenated("/manifest.json").str());
	manifestOut << manifest.dump();

	return 0;
}
