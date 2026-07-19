#include "FileSystem.h"

#include <set>

#include <chrono>
#include <filesystem>
#include <system_error>

#include "utilityString.h"

std::vector<FilePath> FileSystem::getFilePathsFromDirectory(const FilePath &path, const std::vector<std::string> &extensions)
{
	std::set<std::string> ext(extensions.begin(), extensions.end());
	std::vector<FilePath> files;

	if (path.isDirectory())
	{
		std::error_code ec;
		std::filesystem::recursive_directory_iterator it(path.getPath(), ec);
		std::filesystem::recursive_directory_iterator endit;
		for (; it != endit; it.increment(ec))
		{
			ec.clear();

			std::error_code symlinkEc;
			if (std::filesystem::is_symlink(*it, symlinkEc))
			{
				// check for self-referencing symlinks
				std::filesystem::path p = std::filesystem::read_symlink(*it, symlinkEc);
				if (!symlinkEc && p.filename() == p.string() && p.filename() == it->path().filename())
				{
					it.disable_recursion_pending();
					continue;
				}
			}

			std::error_code fileEc;
			if (std::filesystem::is_regular_file(*it, fileEc) && (ext.empty() || ext.find(it->path().extension().string()) != ext.end()))
				files.push_back(FilePath(it->path().generic_string()));
		}
	}
	return files;
}

FileInfo FileSystem::getFileInfoForPath(const FilePath &filePath)
{
	if (filePath.exists())
	{
		return FileInfo(filePath, getLastWriteTime(filePath));
	}
	return FileInfo();
}

std::vector<FileInfo> FileSystem::getFileInfosFromPaths(const std::vector<FilePath> &paths, const std::vector<std::string> &fileExtensions,
	bool followSymLinks)
{
	std::set<std::string> ext;
	for (const std::string &e : fileExtensions)
	{
		ext.insert(utility::toLowerCase(e));
	}

	std::set<std::filesystem::path> symlinkDirs;
	std::set<FilePath> filePaths;

	std::vector<FileInfo> files;

	for (const FilePath &path : paths)
	{
		if (path.isDirectory())
		{
			std::error_code ec;

			// Seed with the starting directory so symlinks back to it are detected as duplicates
			std::filesystem::path startCanonical = std::filesystem::canonical(path.getPath(), ec);
			if (!ec)
				symlinkDirs.insert(startCanonical);
			ec.clear();

			std::filesystem::recursive_directory_iterator it(path.getPath(), std::filesystem::directory_options::follow_directory_symlink, ec);
			std::filesystem::recursive_directory_iterator endit;
			for (; it != endit; it.increment(ec))
			{
				ec.clear();

				std::error_code symlinkEc;
				if (std::filesystem::is_symlink(*it, symlinkEc))
				{
					if (!followSymLinks)
					{
						it.disable_recursion_pending();
						continue;
					}

					// check for self-referencing symlinks
					std::filesystem::path p = std::filesystem::read_symlink(*it, symlinkEc);
					if (!symlinkEc && p.filename() == p.string() && p.filename() == it->path().filename())
					{
						it.disable_recursion_pending();
						continue;
					}

					// check for duplicates when following directory symlinks
					std::error_code dirEc;
					if (std::filesystem::is_directory(*it, dirEc))
					{
						std::filesystem::path absDir = std::filesystem::canonical(it->path().parent_path() / p, dirEc);
						if (dirEc)
							continue;

						if (symlinkDirs.find(absDir) != symlinkDirs.end())
						{
							it.disable_recursion_pending();
							continue;
						}

						symlinkDirs.insert(absDir);
					}
				}

				std::error_code fileEc;
				if (std::filesystem::is_regular_file(*it, fileEc) && (ext.empty() || ext.find(utility::toLowerCase(it->path().extension().string())) != ext.end()))
				{
					const FilePath canonicalPath = FilePath(it->path().string()).getCanonical();
					if (filePaths.find(canonicalPath) != filePaths.end())
						continue;
					filePaths.insert(canonicalPath);
					files.push_back(getFileInfoForPath(canonicalPath));
				}
			}
		}
		else if (path.exists() && (ext.empty() || ext.find(utility::toLowerCase(path.extension())) != ext.end()))
		{
			const FilePath canonicalPath = path.getCanonical();
			if (filePaths.find(canonicalPath) != filePaths.end())
			{
				continue;
			}
			filePaths.insert(canonicalPath);
			files.push_back(getFileInfoForPath(canonicalPath));
		}
	}

	return files;
}

std::set<FilePath> FileSystem::getSymLinkedDirectories(const FilePath &path)
{
	return getSymLinkedDirectories(std::vector<FilePath>{path});
}

std::set<FilePath> FileSystem::getSymLinkedDirectories(const std::vector<FilePath> &paths)
{
	std::set<std::filesystem::path> symlinkDirs;

	for (const FilePath &path : paths)
	{
		if (path.isDirectory())
		{
			std::error_code ec;
			std::filesystem::recursive_directory_iterator it(path.getPath(), std::filesystem::directory_options::follow_directory_symlink, ec);
			std::filesystem::recursive_directory_iterator endit;
			for (; it != endit; it.increment(ec))
			{
				ec.clear();

				std::error_code symlinkEc;
				if (std::filesystem::is_symlink(*it, symlinkEc))
				{
					// check for self-referencing symlinks
					std::filesystem::path p = std::filesystem::read_symlink(*it, symlinkEc);
					if (!symlinkEc && p.filename() == p.string() && p.filename() == it->path().filename())
					{
						it.disable_recursion_pending();
						continue;
					}

					// check for duplicates when following directory symlinks
					std::error_code dirEc;
					if (std::filesystem::is_directory(*it, dirEc))
					{
						std::filesystem::path absDir = std::filesystem::canonical(it->path().parent_path() / p, dirEc);
						if (dirEc)
							continue;

						if (symlinkDirs.find(absDir) != symlinkDirs.end())
						{
							it.disable_recursion_pending();
							continue;
						}

						symlinkDirs.insert(absDir);
					}
				}
			}
		}
	}

	std::set<FilePath> files;
	for (const auto &p : symlinkDirs)
	{
		files.insert(FilePath(p.string()));
	}
	return files;
}

unsigned long long FileSystem::getFileByteSize(const FilePath &filePath)
{
	return std::filesystem::file_size(filePath.getPath());
}

TimeStamp FileSystem::getLastWriteTime(const FilePath &filePath)
{
	if (filePath.exists())
	{
		auto ft = std::filesystem::last_write_time(filePath.getPath());
#if defined(_LIBCPP_VERSION)
		// libc++ does not implement std::chrono::clock_cast yet; file_clock::to_sys
		// is the spelling it provides. MSVC conversely lacks file_clock::to_sys.
		auto sysTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
			std::chrono::file_clock::to_sys(ft));
#else
		auto sysTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
			std::chrono::clock_cast<std::chrono::system_clock>(ft));
#endif
		return TimeStamp(sysTime);
	}
	return TimeStamp();
}

bool FileSystem::remove(const FilePath &path)
{
	std::error_code ec;
	const bool ret = std::filesystem::remove(path.getPath(), ec);
	path.recheckExists();
	return ret;
}

bool FileSystem::rename(const FilePath &from, const FilePath &to)
{
	if (!from.recheckExists() || to.recheckExists())
	{
		return false;
	}

	std::filesystem::rename(from.getPath(), to.getPath());
	to.recheckExists();
	return true;
}

bool FileSystem::copyFile(const FilePath &from, const FilePath &to)
{
	if (!from.recheckExists() || to.recheckExists())
	{
		return false;
	}

	std::filesystem::copy_file(from.getPath(), to.getPath());
	to.recheckExists();
	return true;
}

bool FileSystem::copyDirectory(const FilePath &from, const FilePath &to)
{
	if (!from.recheckExists() || to.recheckExists())
	{
		return false;
	}

	std::filesystem::copy(from.getPath(), to.getPath(), std::filesystem::copy_options::recursive);
	to.recheckExists();
	return true;
}

void FileSystem::createDirectories(const FilePath &path)
{
	std::filesystem::create_directories(path.str());
	path.recheckExists();
}

std::vector<FilePath> FileSystem::getDirectSubDirectories(const FilePath &path)
{
	std::vector<FilePath> v;

	if (path.exists() && path.isDirectory())
	{
		for (std::filesystem::directory_iterator end, dir(path.str()); dir != end; dir++)
		{
			if (std::filesystem::is_directory(dir->path()))
			{
				v.push_back(FilePath(dir->path().string()));
			}
		}
	}

	return v;
}
