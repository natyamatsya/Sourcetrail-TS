#ifndef USER_PATHS_H
#define USER_PATHS_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
#endif

SRCTRL_EXPORT class UserPaths
{
public:
	static FilePath getUserDataDirectoryPath();
	static void setUserDataDirectoryPath(const FilePath& path);

	static FilePath getAppSettingsFilePath();
	static FilePath getWindowSettingsFilePath();
	static FilePath getLogDirectoryPath();

private:
	static FilePath s_userDataDirectoryPath;
};

#include "UserPaths.inl"

#endif	  // USER_PATHS_H
