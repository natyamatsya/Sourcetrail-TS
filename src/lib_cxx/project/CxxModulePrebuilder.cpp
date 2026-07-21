#include "CxxModulePrebuilder.h"

#include <cctype>
#include <fstream>
#include <string_view>

#include <nlohmann/json.hpp>

#ifndef SRCTRL_MODULE_BUILD
#include "FileSystem.h"
#endif
#include "logging.h"
#include "utilitySourceGroupCxx.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
#endif

namespace
{
// True if `word` occurs in `line` as a whole word (not as part of a longer identifier). A plain
// substring scan for "import" fires on the very common word "important" (and "module" on identifiers
// like "module_count"), so we require word boundaries.
bool containsWord(const std::string& line, std::string_view word)
{
	const auto isWordChar = [](char c) {
		return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
	};
	for (std::size_t pos = line.find(word); pos != std::string::npos; pos = line.find(word, pos + 1))
	{
		const bool leftOk = pos == 0 || !isWordChar(line[pos - 1]);
		const std::size_t end = pos + word.size();
		const bool rightOk = end >= line.size() || !isWordChar(line[end]);
		if (leftOk && rightOk)
		{
			return true;
		}
	}
	return false;
}

// Cheap pre-filter: does the file's text mention a module/import keyword at all? Avoids spawning the
// prebuild subprocess for the common case of a source group with no C++20 modules. Whole-word
// matching keeps the rare false positives (the keyword in a comment/string) -- which only cost a scan
// that finds nothing -- while never missing a real module unit (which always has "module" or "import"
// as a keyword), so there are no false negatives. This is a plain file read -- no parsing -- so it is
// safe to run in the main process.
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
		if (containsWord(line, "module") || containsWord(line, "import"))
		{
			return true;
		}
	}
	return false;
}
}	 // namespace

CxxModulePrebuilder::Result CxxModulePrebuilder::prebuild(
	const std::map<FilePath, std::vector<std::string>>& fileFlags, const FilePath& cacheDir)
{
	Result result;

	// Detect candidate module files without parsing anything; skip entirely (no subprocess) if none.
	nlohmann::json candidateFiles = nlohmann::json::array();
	for (const auto& [file, flags]: fileFlags)
	{
		if (mightUseModules(file))
		{
			candidateFiles.push_back({{"path", file.str()}, {"flags", flags}});
		}
	}
	if (candidateFiles.empty())
	{
		return result;
	}

	// Hand the heavy, parse-arbitrary-user-code work (scan + BMI build) to a sourcetrail_indexer
	// subprocess, so a crash/hang there takes down only the worker -- not the app. Channels are the
	// filesystem: request.json in, manifest.json + the .pcm cache out.
	FileSystem::createDirectories(cacheDir);
	const FilePath requestPath = cacheDir.getConcatenated("/request.json");
	const FilePath manifestPath = cacheDir.getConcatenated("/manifest.json");
	FileSystem::remove(manifestPath);

	nlohmann::json request;
	request["cacheDir"] = cacheDir.str();
	request["files"] = candidateFiles;
	{
		std::ofstream out(requestPath.str());
		out << request.dump();
	}

	const std::expected<void, utility::IndexerPrebuildError> prebuilt =
		utility::runIndexerPrebuildMode("--prebuild-modules=" + requestPath.str());
	if (!prebuilt || !manifestPath.recheckExists())
	{
		LOG_ERROR(
			std::string("C++20 module prebuild failed (") +
			(prebuilt ? "manifest missing" : std::string(utility::to_std_sv(prebuilt.error()))) +
			"); imports will not resolve for this source group.");
		return result;
	}

	// Read back which files are interface units; the module path is cacheDir, which we already know.
	try
	{
		std::ifstream in(manifestPath.str());
		const nlohmann::json manifest = nlohmann::json::parse(in);
		for (const std::string& file: manifest.value("interfaceUnits", std::vector<std::string>{}))
		{
			result.interfaceUnits.insert(FilePath(file));
		}
		// Surface any BMI build failures the (loggerless) prebuild subprocess reported, with the clang
		// diagnostics it captured -- so a broken module is debuggable from the normal index log instead
		// of only by re-running the prebuild by hand.
		for (const auto& failure: manifest.value("failures", nlohmann::json::array()))
		{
			const std::string module = failure.value("module", std::string());
			const std::string file = failure.value("file", std::string());
			const std::string diagnostics = failure.value("diagnostics", std::string());
			LOG_ERROR(
				"C++20 module prebuild: failed to build BMI for '" + module + "'" +
				(file.empty() ? std::string() : " (" + file + ")") +
				"; imports of it will not resolve.\n" + diagnostics);
		}
	}
	catch (const nlohmann::json::exception& e)
	{
		LOG_ERROR(std::string("module prebuild manifest was not valid JSON: ") + e.what());
		return result;
	}

	result.anyModules = true;
	result.sharedFlags = {"-fprebuilt-module-path=" + cacheDir.str()};
	return result;
}
