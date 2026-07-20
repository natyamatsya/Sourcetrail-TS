#ifndef UTILITY_FILE_PATH_H
#define UTILITY_FILE_PATH_H

// The FilePath specialization of utility::toStrings, split out of utility.h so that header stays
// FilePath-free (utility.h is part of srctrl.utility, which srctrl.file imports -- keeping FilePath
// out avoids a module cycle). Classic-build seam only; include it where toStrings(vector<FilePath>)
// is actually called.

#include <string>
#include <vector>

#include "FilePath.h"
#include "utility.h"

namespace utility
{
template <>
inline std::vector<std::string> toStrings<FilePath>(const std::vector<FilePath>& d)
{
	return convert<FilePath, std::string>(d, [](const FilePath& fp) { return fp.str(); });
}
}	 // namespace utility

#endif	  // UTILITY_FILE_PATH_H
