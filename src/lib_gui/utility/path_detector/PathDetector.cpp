#include "PathDetector.h"

#ifndef SRCTRL_MODULE_BUILD
#include "FilePath.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
#endif

PathDetector::PathDetector(const std::string& name): m_name(name) {}

std::string PathDetector::getName() const
{
	return m_name;
}

std::vector<FilePath> PathDetector::getPaths() const
{
	std::vector<FilePath> paths;
	for (const FilePath& path: doGetPaths())
	{
		if (path.exists())
		{
			paths.push_back(path);
		}
	}
	return paths;
}

bool PathDetector::isWorking() const
{
	return (!getPaths().empty());
}
