#include "CommandlineHelper.h"

#ifndef SRCTRL_MODULE_BUILD
#include "utilityString.h"
#endif

#include <string>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.utility;
#endif

namespace commandline
{
std::vector<FilePath> extractPaths(const std::vector<std::string>& vector)
{
	std::vector<FilePath> v;
	for (const std::string& s: vector)
	{
		std::vector<std::string> temp = utility::splitToVector(s, ',');
		for (const std::string& path: temp)
		{
			v.push_back(FilePath(path));
		}
	}
	return v;
}

}	 // namespace commandline
