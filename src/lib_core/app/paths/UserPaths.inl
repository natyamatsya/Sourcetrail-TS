// Inline implementations for UserPaths.h. Included at the end of that header; not a standalone TU.

#pragma once

inline FilePath UserPaths::s_userDataDirectoryPath;

inline FilePath UserPaths::getUserDataDirectoryPath()
{
	return s_userDataDirectoryPath;
}

inline void UserPaths::setUserDataDirectoryPath(const FilePath& path)
{
	s_userDataDirectoryPath = path;
}

inline FilePath UserPaths::getAppSettingsFilePath()
{
	return getUserDataDirectoryPath().concatenate("ApplicationSettings.json");
}

inline FilePath UserPaths::getWindowSettingsFilePath()
{
	return getUserDataDirectoryPath().concatenate("window_settings.ini");
}

inline FilePath UserPaths::getLogDirectoryPath()
{
	return getUserDataDirectoryPath().concatenate("log/");
}
