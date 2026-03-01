#include "utilityPathDetection.h"

#include "language_package_flags.h"

#include "utilityApp.h"

#include "CxxFrameworkPathDetector.h"
#include "CxxHeaderPathDetector.h"
#include "CxxVs17ToLatestHeaderPathDetector.h"
#include "ToolChain.h"

using namespace std;

std::shared_ptr<CombinedPathDetector> utility::getCxxVsHeaderPathDetector()
{
	std::shared_ptr<CombinedPathDetector> combinedDetector = std::make_shared<CombinedPathDetector>();

	if constexpr (!utility::Platform::isWindows())
	{
		return combinedDetector;
	}

	if constexpr (language_packages::buildCxxLanguagePackage)
	for (const string &versionRange : VisualStudio::getVersionRanges())
		combinedDetector->addDetector(make_shared<CxxVs17ToLatestHeaderPathDetector>(versionRange));

	return combinedDetector;
}

std::shared_ptr<CombinedPathDetector> utility::getCxxHeaderPathDetector()
{
	std::shared_ptr<CombinedPathDetector> combinedDetector = getCxxVsHeaderPathDetector();
	if constexpr (language_packages::buildCxxLanguagePackage)
	{
		combinedDetector->addDetector(std::make_shared<CxxHeaderPathDetector>("clang"));
		combinedDetector->addDetector(std::make_shared<CxxHeaderPathDetector>("gcc"));
	}
	return combinedDetector;
}

std::shared_ptr<CombinedPathDetector> utility::getCxxFrameworkPathDetector()
{
	std::shared_ptr<CombinedPathDetector> combinedDetector = std::make_shared<CombinedPathDetector>();
	if constexpr (language_packages::buildCxxLanguagePackage)
	{
		combinedDetector->addDetector(std::make_shared<CxxFrameworkPathDetector>("clang"));
		combinedDetector->addDetector(std::make_shared<CxxFrameworkPathDetector>("gcc"));
	}
	return combinedDetector;
}
