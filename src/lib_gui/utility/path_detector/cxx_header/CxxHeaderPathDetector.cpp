#include "CxxHeaderPathDetector.h"

#include "FilePath.h"
#include "utilityCxxHeaderDetection.h"
#include "utilityString.h"

CxxHeaderPathDetector::CxxHeaderPathDetector(const std::string& compilerName)
	: PathDetector(compilerName), m_compilerName(compilerName)
{
}

std::vector<FilePath> CxxHeaderPathDetector::doGetPaths() const
{
	std::vector<std::string> paths = utility::getCxxHeaderPaths(m_compilerName);
	std::vector<FilePath> headerSearchPaths;

	for (const std::string& path: paths)
	{
		// Skip an SDK's usr/include on macOS: it is supplied by -isysroot, and adding
		// it as an explicit -isystem breaks libc++'s #include_next chain (see
		// IndexerCommandCxx::getCompilerFlagsForSystemHeaderSearchPaths). The indexer
		// strips it defensively too, but never storing it keeps the settings clean.
		if (utility::isPostfix("/usr/include", path) && path.find(".sdk/") != std::string::npos)
		{
			continue;
		}

		if (!utility::isPostfix(" (framework directory)", path) &&
			FilePath(path).getCanonical().exists() &&
			!FilePath(path).getCanonical().getConcatenated("/stdarg.h").exists())
		{
			headerSearchPaths.push_back(FilePath(path).makeCanonical());
		}
	}

	return headerSearchPaths;
}
