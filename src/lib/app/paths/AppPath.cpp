#include "AppPath.h"

#include "utilityApp.h"

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
	std::string cxxIndexerName("sourcetrail_indexer" + FilePath::getExecutableExtension());

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
	std::string name("sourcetrail_rust_indexer" + FilePath::getExecutableExtension());

	if (!s_cxxIndexerDirectoryPath.empty())
		return s_cxxIndexerDirectoryPath.getConcatenated(name);
	else
		return s_sharedDataDirectoryPath.getConcatenated(name);
}

FilePath AppPath::getSwiftIndexerFilePath()
{
	std::string name("sourcetrail_swift_indexer" + FilePath::getExecutableExtension());

	if (!s_cxxIndexerDirectoryPath.empty())
		return s_cxxIndexerDirectoryPath.getConcatenated(name);
	else
		return s_sharedDataDirectoryPath.getConcatenated(name);
}

FilePath AppPath::getZigIndexerFilePath()
{
	std::string name("sourcetrail_zig_indexer" + FilePath::getExecutableExtension());

	if (!s_cxxIndexerDirectoryPath.empty())
		return s_cxxIndexerDirectoryPath.getConcatenated(name);
	else
		return s_sharedDataDirectoryPath.getConcatenated(name);
}
