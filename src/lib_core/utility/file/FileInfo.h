#ifndef FILE_INFO_H
#define FILE_INFO_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "FilePath.h"
#include "TimeStamp.h"
#endif

SRCTRL_EXPORT struct FileInfo
{
	FileInfo();
	FileInfo(const FilePath& path);
	FileInfo(const FilePath& path, const TimeStamp& lastWriteTime);

	FilePath path;
	TimeStamp lastWriteTime;
};

#include "FileInfo.inl"

#endif	  // FILE_INFO_H
