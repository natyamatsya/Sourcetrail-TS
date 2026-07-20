// Inline implementations for AppPath.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <format>
#include "Platform.h"
#endif

inline FilePath AppPath::s_sharedDataDirectoryPath("");
inline FilePath AppPath::s_cxxIndexerDirectoryPath("");

inline FilePath AppPath::getSharedDataDirectoryPath()
{
	return s_sharedDataDirectoryPath;
}

inline void AppPath::setSharedDataDirectoryPath(const FilePath& path)
{
	s_sharedDataDirectoryPath = path;
}

inline FilePath AppPath::getCxxIndexerFilePath()
{
	std::string cxxIndexerName(std::format("sourcetrail_indexer{}", utility::Platform::getExecutableExtension()));

	if (!s_cxxIndexerDirectoryPath.empty())
		return s_cxxIndexerDirectoryPath.getConcatenated(cxxIndexerName);
	else
		return s_sharedDataDirectoryPath.getConcatenated(cxxIndexerName);
}

inline void AppPath::setCxxIndexerDirectoryPath(const FilePath& path)
{
	s_cxxIndexerDirectoryPath = path;
}

inline FilePath AppPath::getRustIndexerFilePath()
{
	std::string name(std::format("sourcetrail_rust_indexer{}", utility::Platform::getExecutableExtension()));

	if (!s_cxxIndexerDirectoryPath.empty())
		return s_cxxIndexerDirectoryPath.getConcatenated(name);
	else
		return s_sharedDataDirectoryPath.getConcatenated(name);
}

inline FilePath AppPath::getSwiftIndexerFilePath()
{
	std::string name(std::format("sourcetrail_swift_indexer{}", utility::Platform::getExecutableExtension()));

	if (!s_cxxIndexerDirectoryPath.empty())
		return s_cxxIndexerDirectoryPath.getConcatenated(name);
	else
		return s_sharedDataDirectoryPath.getConcatenated(name);
}

inline FilePath AppPath::getZigIndexerFilePath()
{
	std::string name(std::format("sourcetrail_zig_indexer{}", utility::Platform::getExecutableExtension()));

	if (!s_cxxIndexerDirectoryPath.empty())
		return s_cxxIndexerDirectoryPath.getConcatenated(name);
	else
		return s_sharedDataDirectoryPath.getConcatenated(name);
}
