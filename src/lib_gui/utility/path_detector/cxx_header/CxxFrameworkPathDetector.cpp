#include "CxxFrameworkPathDetector.h"

#ifndef SRCTRL_MODULE_BUILD
#include "FilePath.h"
#endif
#include "utilityCxxHeaderDetection.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utilityString.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.utility;
#endif

CxxFrameworkPathDetector::CxxFrameworkPathDetector(const std::string& compilerName)
	: PathDetector(compilerName), m_compilerName(compilerName)
{
}

std::vector<FilePath> CxxFrameworkPathDetector::doGetPaths() const
{
	std::vector<std::string> paths = utility::getCxxHeaderPaths(m_compilerName);
	std::vector<FilePath> frameworkPaths;
	for (const std::string& path: paths)
	{
		if (utility::isPostfix(" (framework directory)", path))
		{
			FilePath p =
				FilePath(utility::replace(path, " (framework directory)", "")).makeCanonical();
			if (p.exists())
			{
				frameworkPaths.push_back(p);
			}
		}
	}
	return frameworkPaths;
}
