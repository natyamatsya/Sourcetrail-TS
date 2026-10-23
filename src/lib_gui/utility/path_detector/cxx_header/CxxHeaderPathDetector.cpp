#include "CxxHeaderPathDetector.h"

#ifndef SRCTRL_MODULE_BUILD
#include "FilePath.h"
#endif
#include "Platform.h"
#include "ToolChain.h"
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
		// Skip what the macOS driver supplies itself: an SDK's usr/include, and the
		// libc++ of the detected compiler, which need not be the indexer's (`clang`
		// on PATH can be Homebrew LLVM while the indexer emulates Apple's `c++`).
		// Adding them as explicit -isystem breaks libc++'s #include_next chain (see
		// IndexerCommandCxx::getCompilerFlagsForSystemHeaderSearchPaths). The indexer
		// strips them defensively too, but never storing them keeps the settings clean.
		if constexpr (utility::Platform::isMac())
		{
			if (ClangCompiler::isSuppliedByMacSysroot(FilePath(path).getCanonical().str()))
			{
				continue;
			}
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
