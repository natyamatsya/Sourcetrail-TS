#include "AppPath.h"

#include "Platform.h"
#include "utilityApp.h"

#include <format>

FilePath AppPath::s_sharedDataDirectoryPath("");
FilePath AppPath::s_cxxIndexerDirectoryPath("");

FilePath AppPath::getSharedDataDirectoryPath()
{
	return s_sharedDataDirectoryPath;
}

void AppPath::setSharedDataDirectoryPath(const FilePath& path)
{
	s_sharedDataDirectoryPath = path;
}

FilePath AppPath::getCxxIndexerFilePath()
{
	std::string cxxIndexerName(std::format("sourcetrail_indexer{}", utility::Platform::getExecutableExtension()));

	if (!s_cxxIndexerDirectoryPath.empty())
		return s_cxxIndexerDirectoryPath.getConcatenated(cxxIndexerName);
	else
		return s_sharedDataDirectoryPath.getConcatenated(cxxIndexerName);
}

void AppPath::setCxxIndexerDirectoryPath(const FilePath& path)
{
	s_cxxIndexerDirectoryPath = path;
}

FilePath AppPath::getRustIndexerFilePath()
{
	std::string name(std::format("sourcetrail_rust_indexer{}", utility::Platform::getExecutableExtension()));

	if (!s_cxxIndexerDirectoryPath.empty())
		return s_cxxIndexerDirectoryPath.getConcatenated(name);
	else
		return s_sharedDataDirectoryPath.getConcatenated(name);
}

FilePath AppPath::getSwiftIndexerFilePath()
{
	std::string name(std::format("sourcetrail_swift_indexer{}", utility::Platform::getExecutableExtension()));

	if (!s_cxxIndexerDirectoryPath.empty())
		return s_cxxIndexerDirectoryPath.getConcatenated(name);
	else
		return s_sharedDataDirectoryPath.getConcatenated(name);
}

FilePath AppPath::getZigIndexerFilePath()
{
	std::string name(std::format("sourcetrail_zig_indexer{}", utility::Platform::getExecutableExtension()));

	if (!s_cxxIndexerDirectoryPath.empty())
		return s_cxxIndexerDirectoryPath.getConcatenated(name);
	else
		return s_sharedDataDirectoryPath.getConcatenated(name);
}
