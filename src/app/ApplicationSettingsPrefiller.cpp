#include "ApplicationSettingsPrefiller.h"

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#include "FilePath.h"
#include "MessageStatus.h"
#endif
#include "logging.h"
#include "utilityPathDetection.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utilityString.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.messaging;
import srctrl.settings;
import srctrl.utility;
#endif

void ApplicationSettingsPrefiller::prefillPaths(ApplicationSettings* settings)
{
	bool updated = false;

	updated |= migrateStaleSdkHeaderPaths(settings);
	updated |= prefillCxxHeaderPaths(settings);
	updated |= prefillCxxFrameworkPaths(settings);

	if (updated)
	{
		settings->save();
	}
}

bool ApplicationSettingsPrefiller::migrateStaleSdkHeaderPaths(ApplicationSettings* settings)
{
	const std::vector<FilePath> paths = settings->getHeaderSearchPaths();
	std::vector<FilePath> cleaned;
	cleaned.reserve(paths.size());
	for (const FilePath& path: paths)
	{
		const std::string s = path.str();
		// Drop any SDK usr/include: it is provided by -isysroot, and keeping it as an
		// explicit -isystem breaks libc++'s #include_next chain during indexing.
		if (utility::isPostfix("/usr/include", s) && s.find(".sdk/") != std::string::npos)
		{
			continue;
		}
		cleaned.push_back(path);
	}

	if (cleaned.size() == paths.size())
	{
		return false;
	}

	LOG_INFO(
		"Removed " + std::to_string(paths.size() - cleaned.size()) +
		" stale SDK usr/include header search path(s); the SDK sysroot supplies them.");
	settings->setHeaderSearchPaths(cleaned);
	return true;
}

bool ApplicationSettingsPrefiller::prefillCxxHeaderPaths(ApplicationSettings* settings)
{
	if (settings->getHasPrefilledHeaderSearchPaths())	 // allow empty
	{
		return false;
	}

	LOG_INFO("Prefilling header search paths");
	std::shared_ptr<CombinedPathDetector> cxxHeaderDetector = utility::getCxxHeaderPathDetector();
	std::vector<FilePath> paths = cxxHeaderDetector->getPaths();
	if (!paths.empty())
	{
		MessageStatus(
			"Ran C/C++ header path detection, found " + std::to_string(paths.size()) + " path" +
			(paths.size() == 1 ? "" : "s"))
			.dispatch();

		settings->setHeaderSearchPaths(paths);
	}

	settings->setHasPrefilledHeaderSearchPaths(true);
	return true;
}

bool ApplicationSettingsPrefiller::prefillCxxFrameworkPaths(ApplicationSettings* settings)
{
	if (settings->getHasPrefilledFrameworkSearchPaths())	// allow empty
	{
		return false;
	}

	LOG_INFO("Prefilling framework search paths");
	std::shared_ptr<CombinedPathDetector> cxxFrameworkDetector =
		utility::getCxxFrameworkPathDetector();
	std::vector<FilePath> paths = cxxFrameworkDetector->getPaths();
	if (!paths.empty())
	{
		MessageStatus(
			"Ran C/C++ framework path detection, found " + std::to_string(paths.size()) +
			" path" + (paths.size() == 1 ? "" : "s"))
			.dispatch();

		settings->setFrameworkSearchPaths(paths);
	}

	settings->setHasPrefilledFrameworkSearchPaths(true);
	return true;
}
