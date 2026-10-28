// Inline implementations for FileSystem.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "logging.h"

#include <cctype>
#include <chrono>
#include <expected>
#include <filesystem>
#include <system_error>
#endif

namespace file_system_detail
{
// ASCII lowercasing for file-extension matching (extensions are ASCII in practice). Replaces
// utility::toLowerCase -- a Qt/locale-based helper deliberately excluded from srctrl.utility:string,
// which this .inl cannot reach from the srctrl.file purview. Mirrors FilePath::getLowerCase().
inline std::string toLowerCaseAscii(std::string in)
{
	for (char& c: in)
	{
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return in;
}

// Whether a recursive walk may enter the directory `entry`. One it cannot read (no permission) is
// skipped with a warning naming it: the walk's increment into it failed and, failing, ended the
// whole walk, so one `chmod 000` subfolder made a source group find no files at all.
inline bool canEnterDirectory(const std::filesystem::directory_entry& entry)
{
	std::error_code ec;
	if (!entry.is_directory(ec))
	{
		return true;	// nothing to enter
	}
	const std::filesystem::directory_iterator probe(entry.path(), ec);
	if (ec)
	{
		LOG_WARNING_STREAM(
			<< "Skipping the directory \"" << entry.path().generic_string() << "\": " << ec.message());
		return false;
	}
	return true;
}

// Opens a recursive walk of `path`; a root it cannot read is warned about, and walks nothing.
inline std::filesystem::recursive_directory_iterator walkDirectory(
	const FilePath& path, std::filesystem::directory_options options)
{
	std::error_code ec;
	std::filesystem::recursive_directory_iterator it(
		path.getPath(), options | std::filesystem::directory_options::skip_permission_denied, ec);
	if (ec)
	{
		LOG_WARNING_STREAM(<< "Skipping the directory \"" << path.str() << "\": " << ec.message());
		return {};
	}
	return it;
}

// Advances a recursive walk, first keeping it out of a directory it cannot read. A failed increment
// still ends the walk (the iterator becomes the end), but says so.
inline void advanceWalk(std::filesystem::recursive_directory_iterator& it)
{
	if (it.recursion_pending() && !canEnterDirectory(*it))
	{
		it.disable_recursion_pending();
	}
	std::error_code ec;
	const std::filesystem::path current = it->path();
	it.increment(ec);
	if (ec)
	{
		LOG_WARNING_STREAM(
			<< "Stopped listing files after \"" << current.generic_string() << "\": " << ec.message());
	}
}
}	 // namespace file_system_detail

inline std::vector<FilePath> FileSystem::getFilePathsFromDirectory(const FilePath &path, const std::vector<std::string> &extensions)
{
	std::set<std::string> ext(extensions.begin(), extensions.end());
	std::vector<FilePath> files;

	if (path.isDirectory())
	{
		std::filesystem::recursive_directory_iterator it =
			file_system_detail::walkDirectory(path, std::filesystem::directory_options::none);
		std::filesystem::recursive_directory_iterator endit;
		for (; it != endit; file_system_detail::advanceWalk(it))
		{
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

inline FileInfo FileSystem::getFileInfoForPath(const FilePath &filePath)
{
	if (filePath.exists())
	{
		return FileInfo(filePath, getLastWriteTime(filePath));
	}
	return FileInfo();
}

inline std::vector<FileInfo> FileSystem::getFileInfosFromPaths(const std::vector<FilePath> &paths, const std::vector<std::string> &fileExtensions,
	bool followSymLinks)
{
	std::set<std::string> ext;
	for (const std::string &e : fileExtensions)
	{
		ext.insert(file_system_detail::toLowerCaseAscii(e));
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

			std::filesystem::recursive_directory_iterator it = file_system_detail::walkDirectory(
				path, std::filesystem::directory_options::follow_directory_symlink);
			std::filesystem::recursive_directory_iterator endit;
			for (; it != endit; file_system_detail::advanceWalk(it))
			{
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
				if (std::filesystem::is_regular_file(*it, fileEc) && (ext.empty() || ext.find(file_system_detail::toLowerCaseAscii(it->path().extension().string())) != ext.end()))
				{
					const FilePath canonicalPath = FilePath(it->path().string()).getCanonical();
					if (filePaths.find(canonicalPath) != filePaths.end())
						continue;
					filePaths.insert(canonicalPath);
					files.push_back(getFileInfoForPath(canonicalPath));
				}
			}
		}
		else if (path.exists() && (ext.empty() || ext.find(file_system_detail::toLowerCaseAscii(path.extension())) != ext.end()))
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

inline std::set<FilePath> FileSystem::getSymLinkedDirectories(const FilePath &path)
{
	return getSymLinkedDirectories(std::vector<FilePath>{path});
}

inline std::set<FilePath> FileSystem::getSymLinkedDirectories(const std::vector<FilePath> &paths)
{
	std::set<std::filesystem::path> symlinkDirs;

	for (const FilePath &path : paths)
	{
		if (path.isDirectory())
		{
			std::filesystem::recursive_directory_iterator it = file_system_detail::walkDirectory(
				path, std::filesystem::directory_options::follow_directory_symlink);
			std::filesystem::recursive_directory_iterator endit;
			for (; it != endit; file_system_detail::advanceWalk(it))
			{
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

inline unsigned long long FileSystem::getFileByteSize(const FilePath &filePath)
{
	// 0 for a file whose size cannot be read (gone, a symlink loop, no permission) -- never a throw.
	std::error_code ec;
	const std::uintmax_t size = std::filesystem::file_size(filePath.getPath(), ec);
	return ec ? 0 : size;
}

inline TimeStamp FileSystem::getLastWriteTime(const FilePath &filePath)
{
	std::error_code ec;
	if (const auto ft = std::filesystem::last_write_time(filePath.getPath(), ec); !ec)
	{
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

inline bool FileSystem::remove(const FilePath &path)
{
	std::error_code ec;
	const bool ret = std::filesystem::remove(path.getPath(), ec);
	path.recheckExists();
	return ret;
}

inline bool FileSystem::rename(const FilePath &from, const FilePath &to)
{
	if (!from.recheckExists() || to.recheckExists())
	{
		return false;
	}

	std::error_code ec;
	std::filesystem::rename(from.getPath(), to.getPath(), ec);
	from.recheckExists();
	to.recheckExists();
	return !ec;
}

inline bool FileSystem::copyFile(const FilePath &from, const FilePath &to)
{
	if (!from.recheckExists() || to.recheckExists())
	{
		return false;
	}

	std::error_code ec;
	std::filesystem::copy_file(from.getPath(), to.getPath(), ec);
	to.recheckExists();
	return !ec;
}

inline bool FileSystem::copyDirectory(const FilePath &from, const FilePath &to)
{
	if (!from.recheckExists() || to.recheckExists())
	{
		return false;
	}

	std::error_code ec;
	std::filesystem::copy(from.getPath(), to.getPath(), std::filesystem::copy_options::recursive, ec);
	to.recheckExists();
	return !ec;
}

inline std::expected<void, std::error_code> FileSystem::createDirectories(const FilePath &path)
{
	std::error_code ec;
	std::filesystem::create_directories(path.getPath(), ec);
	path.recheckExists();
	if (ec)
	{
		return std::unexpected(ec);
	}
	return {};
}

inline std::vector<FilePath> FileSystem::getDirectSubDirectories(const FilePath &path)
{
	std::vector<FilePath> v;

	if (path.exists() && path.isDirectory())
	{
		std::error_code ec;
		for (std::filesystem::directory_iterator end, dir(path.getPath(), ec); !ec && dir != end; dir.increment(ec))
		{
			std::error_code dirEc;
			if (std::filesystem::is_directory(dir->path(), dirEc))
			{
				v.push_back(FilePath(dir->path().string()));
			}
		}
	}

	return v;
}
