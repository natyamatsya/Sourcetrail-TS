#include "CxxModulePrebuilder.h"

#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

#include <llvm/TargetParser/Host.h>

#include "FileSystem.h"
#include "logging.h"
#include "utility.h"
#include "utilityApp.h"

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

// The flags a module-interface BMI must be built with AND every importing TU must be indexed with,
// so libclang can load the BMI (target triple + builtin-module handling must match).
std::vector<std::string> moduleMatchFlags(const std::string& triple)
{
	return {"-target", triple, "-Xclang", "-fbuiltin-headers-in-system-modules"};
}
}	 // namespace

CxxModulePrebuilder::Result CxxModulePrebuilder::prebuild(
	const std::set<FilePath>& sourceFiles,
	const std::vector<std::string>& baseCompilerFlags,
	const FilePath& cacheDir)
{
	Result result;

	const std::string triple = llvm::sys::getDefaultTargetTriple();
	const std::string clangxx = toolPath("clang++");
	const std::string scandeps = toolPath("clang-scan-deps");

	// Flags common to scanning and BMI building: the project's own flags, a C++20+ standard, and the
	// match flags. Kept separate from Sourcetrail's include-pch flags, which don't apply here.
	std::vector<std::string> buildFlags = baseCompilerFlags;
	if (!hasStdFlag(buildFlags))
	{
		buildFlags.push_back("-std=c++20");
	}
	utility::append(buildFlags, moduleMatchFlags(triple));

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

		std::vector<std::string> args = buildFlags;
		utility::append(
			args,
			{"-fprebuilt-module-path=" + cacheDir.str(),
			 "-x",
			 "c++-module",
			 "--precompile",
			 file.str(),
			 "-o",
			 bmiPath.str()});

		const utility::ProcessOutput out = utility::executeProcess(clangxx, args, FilePath(), false);
		if (out.exitCode != 0 || !bmiPath.recheckExists())
		{
			LOG_ERROR("Failed to build BMI for module '" + logicalName + "' (" + file.str() + "): " + out.error);
		}
	}

	// --- 4. flags the indexer needs to resolve imports against the cache --------------------------
	result.sharedFlags = {"-fprebuilt-module-path=" + cacheDir.str()};
	utility::append(result.sharedFlags, moduleMatchFlags(triple));
	for (const auto& [file, name]: providedModule)
	{
		result.interfaceUnits.insert(file);
	}
	return result;
}
