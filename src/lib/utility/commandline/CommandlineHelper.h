#ifndef COMMANDLINE_HELPER_H
#define COMMANDLINE_HELPER_H

#include "FilePath.h"

#include <string>
#include <vector>

namespace commandline
{
std::vector<FilePath> extractPaths(const std::vector<std::string>& vector);
}	 // namespace commandline

#endif	  // COMMANDLINE_HELPER_H
