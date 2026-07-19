#include "CommandlineHelper.h"

#include "utilityString.h"

#include <string>

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
