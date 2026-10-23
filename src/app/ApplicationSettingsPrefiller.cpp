#include "ApplicationSettingsPrefiller.h"

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#include "FilePath.h"
#include "MessageStatus.h"
#endif
#include "Platform.h"
#include "ToolChain.h"
#include "logging.h"
#include "utilityPathDetection.h"

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

	updated |= migrateSysrootSuppliedPaths(settings);
	updated |= prefillCxxHeaderPaths(settings);
	updated |= prefillCxxFrameworkPaths(settings);

	if (updated)
	{
		settings->save();
	}
}

bool ApplicationSettingsPrefiller::migrateSysrootSuppliedPaths(ApplicationSettings* settings)
{
	if constexpr (!utility::Platform::isMac())
	{
		return false;
	}

	// The SDK's headers and frameworks and the compiler's libc++ come with
	// -isysroot and the driver; stored again they come first and can belong to
	// another toolchain (see ClangCompiler::isSuppliedByMacSysroot).
	const auto withoutSysrootSupplied = [](const std::vector<FilePath>& paths) {
		std::vector<FilePath> kept;
		kept.reserve(paths.size());
		for (const FilePath& path: paths)
		{
			if (!ClangCompiler::isSuppliedByMacSysroot(path.str()))
			{
				kept.push_back(path);
			}
		}
		return kept;
	};

	const std::vector<FilePath> headerPaths = settings->getHeaderSearchPaths();
	const std::vector<FilePath> frameworkPaths = settings->getFrameworkSearchPaths();
	const std::vector<FilePath> keptHeaderPaths = withoutSysrootSupplied(headerPaths);
	const std::vector<FilePath> keptFrameworkPaths = withoutSysrootSupplied(frameworkPaths);
	const size_t removed = (headerPaths.size() - keptHeaderPaths.size()) +
		(frameworkPaths.size() - keptFrameworkPaths.size());
	if (removed == 0)
	{
		return false;
	}

	LOG_INFO(
		"Removed " + std::to_string(removed) +
		" header or framework search path(s) that the macOS SDK sysroot and the compiler supply.");
	settings->setHeaderSearchPaths(keptHeaderPaths);
	settings->setFrameworkSearchPaths(keptFrameworkPaths);
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
