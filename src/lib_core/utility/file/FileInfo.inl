// Inline implementations for FileInfo.h. Included at the end of that header; not a standalone TU.

#pragma once

inline FileInfo::FileInfo(): path(FilePath("")) {}

inline FileInfo::FileInfo(const FilePath& path): path(path) {}

inline FileInfo::FileInfo(const FilePath& path, const TimeStamp& lastWriteTime)
	: path(path), lastWriteTime(lastWriteTime)
{
}
