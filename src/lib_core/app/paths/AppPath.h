#ifndef APP_PATH_H
#define APP_PATH_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
#endif

SRCTRL_EXPORT class AppPath
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

#include "AppPath.inl"

#endif	  // APP_PATH_H
