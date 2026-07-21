#include "utilityPathDetection.h"
// Classic header, previously supplied transitively by a now-guarded include (rule 5).
#include "Platform.h"

#include "language_package_flags.h"

#ifndef SRCTRL_MODULE_BUILD
#include "utilityApp.h"
#endif

#if BUILD_CXX_LANGUAGE_PACKAGE
	#include "CxxFrameworkPathDetector.h"
	#include "CxxHeaderPathDetector.h"
	#include "CxxVs17ToLatestHeaderPathDetector.h"
#endif
#include "ToolChain.h"
// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.process;
#endif


using namespace std;

std::shared_ptr<CombinedPathDetector> utility::getCxxVsHeaderPathDetector()
{
	std::shared_ptr<CombinedPathDetector> combinedDetector = std::make_shared<CombinedPathDetector>();

	if constexpr (!utility::Platform::isWindows())
	{
		return combinedDetector;
	}

#if BUILD_CXX_LANGUAGE_PACKAGE
	for (const string &versionRange : VisualStudio::getVersionRanges())
		combinedDetector->addDetector(make_shared<CxxVs17ToLatestHeaderPathDetector>(versionRange));
#endif

	return combinedDetector;
}

std::shared_ptr<CombinedPathDetector> utility::getCxxHeaderPathDetector()
{
	std::shared_ptr<CombinedPathDetector> combinedDetector = getCxxVsHeaderPathDetector();
#if BUILD_CXX_LANGUAGE_PACKAGE
	combinedDetector->addDetector(std::make_shared<CxxHeaderPathDetector>("clang"));
	combinedDetector->addDetector(std::make_shared<CxxHeaderPathDetector>("gcc"));
#endif
	return combinedDetector;
}

std::shared_ptr<CombinedPathDetector> utility::getCxxFrameworkPathDetector()
{
	std::shared_ptr<CombinedPathDetector> combinedDetector = std::make_shared<CombinedPathDetector>();
#if BUILD_CXX_LANGUAGE_PACKAGE
	combinedDetector->addDetector(std::make_shared<CxxFrameworkPathDetector>("clang"));
	combinedDetector->addDetector(std::make_shared<CxxFrameworkPathDetector>("gcc"));
#endif

	return combinedDetector;
}
