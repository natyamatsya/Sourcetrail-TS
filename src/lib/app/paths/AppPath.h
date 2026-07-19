#ifndef APP_PATH_H
#define APP_PATH_H

#include "FilePath.h"

class AppPath
{
public:
	static FilePath getSharedDataDirectoryPath();
	static void setSharedDataDirectoryPath(const FilePath& path);

	static FilePath getCxxIndexerFilePath();
	static void setCxxIndexerDirectoryPath(const FilePath& path);

	static FilePath getRustIndexerFilePath();
	static FilePath getSwiftIndexerFilePath();
	static FilePath getZigIndexerFilePath();

private:
	static FilePath s_sharedDataDirectoryPath;
	static FilePath s_cxxIndexerDirectoryPath;
};

#endif	  // APP_PATH_H
