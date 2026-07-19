#include "CxxModulePrebuilder.h"

#include <fstream>

#include <nlohmann/json.hpp>

#include "FileSystem.h"
#include "logging.h"
#include "utilitySourceGroupCxx.h"

namespace
{
// Cheap pre-filter: does the file's text mention a module/import at all? Avoids spawning the
// prebuild subprocess for the common case of a source group with no C++20 modules. False positives
// (the word appears in a comment/string) only cost a scan that finds nothing; a real module unit
// always contains "module" or "import", so there are no false negatives. This is a plain file read
// -- no parsing -- so it is safe to run in the main process.
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
