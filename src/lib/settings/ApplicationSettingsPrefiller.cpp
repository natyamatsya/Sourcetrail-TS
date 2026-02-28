#include "ApplicationSettingsPrefiller.h"

#include "ApplicationSettings.h"
#include "MessageStatus.h"
#include "logging.h"
#include "utilityPathDetection.h"

void ApplicationSettingsPrefiller::prefillPaths(ApplicationSettings* settings)
{
	bool updated = false;

	updated |= prefillCxxHeaderPaths(settings);
	updated |= prefillCxxFrameworkPaths(settings);

	if (updated)
	{
		settings->save();
	}
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
