#include "Catch2.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef SRCTRL_MODULE_BUILD
#include "FileSystem.h"
#include "utility.h"
#endif
#include "Platform.h"
#include "TestTempDirectory.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.utility;
#endif


namespace
{

/*
bool isInFiles(const std::set<FilePath>& files, const FilePath& filename)
{
	return std::end(files) != files.find(filename);
}
*/

bool isInFileInfos(const std::vector<FileInfo>& infos, const std::string& filename)
{
	for (const FileInfo& info: infos)
	{
		if (info.path.getAbsolute().str() == FilePath(filename).getCanonical().str())
		{
			return true;
		}
	}

	return false;
}

bool isInFileInfos(
	const std::vector<FileInfo>& infos, const std::string& filename, const std::string& filename2)
{
	for (const FileInfo& info: infos)
	{
		if (info.path.str() == FilePath(filename).getCanonical().str() ||
			info.path.str() == FilePath(filename2).getCanonical().str())
		{
			return true;
		}
	}

	return false;
}

}

TEST_CASE("find cpp files")
{
	std::vector<std::string> cppFiles = utility::convert<FilePath, std::string>(
		FileSystem::getFilePathsFromDirectory(FilePath("data/FileSystemTestSuite"), {".cpp"}),
		[](const FilePath& filePath) { return filePath.str(); });

	REQUIRE(cppFiles.size() == 4);
	REQUIRE(utility::containsElement<std::string>(cppFiles, "data/FileSystemTestSuite/main.cpp"));
	REQUIRE(utility::containsElement<std::string>(
		cppFiles, "data/FileSystemTestSuite/Settings/sample.cpp"));
	REQUIRE(
		utility::containsElement<std::string>(cppFiles, "data/FileSystemTestSuite/src/main.cpp"));
	REQUIRE(
		utility::containsElement<std::string>(cppFiles, "data/FileSystemTestSuite/src/test.cpp"));
}

TEST_CASE("find h files")
{
	std::vector<std::string> headerFiles = utility::convert<FilePath, std::string>(
		FileSystem::getFilePathsFromDirectory(FilePath("data/FileSystemTestSuite"), {".h"}),
		[](const FilePath& filePath) { return filePath.str(); });

	REQUIRE(headerFiles.size() == 3);
	REQUIRE(utility::containsElement<std::string>(
		headerFiles, "data/FileSystemTestSuite/tictactoe.h"));
	REQUIRE(utility::containsElement<std::string>(
		headerFiles, "data/FileSystemTestSuite/Settings/player.h"));
	REQUIRE(utility::containsElement<std::string>(
		headerFiles, "data/FileSystemTestSuite/src/test.h"));
}

TEST_CASE("find all source files")
{
	std::vector<std::string> sourceFiles = utility::convert<FilePath, std::string>(
		FileSystem::getFilePathsFromDirectory(
			FilePath("data/FileSystemTestSuite"), {".h", ".hpp", ".cpp"}),
		[](const FilePath& filePath) { return filePath.str(); });

	REQUIRE(sourceFiles.size() == 8);
}

TEST_CASE("find file infos ignore symlinks")
{
	ASSERT_SYMLINK_PLATFORM();

	std::vector<FilePath> directoryPaths;
	directoryPaths.push_back(FilePath("./data/FileSystemTestSuite/src"));

	std::vector<FileInfo> files = FileSystem::getFileInfosFromPaths(
		directoryPaths, {".h", ".hpp", ".cpp"}, false);

	REQUIRE(files.size() == 2);
	REQUIRE(isInFileInfos(files, "./data/FileSystemTestSuite/src/test.cpp"));
	REQUIRE(isInFileInfos(files, "./data/FileSystemTestSuite/src/test.h"));
}

TEST_CASE("find file infos follow symlinks")
{
	ASSERT_SYMLINK_PLATFORM();

	std::vector<FilePath> directoryPaths;
	directoryPaths.push_back(FilePath("./data/FileSystemTestSuite/src"));

	std::vector<FileInfo> files = FileSystem::getFileInfosFromPaths(
		directoryPaths, {".h", ".hpp", ".cpp"}, true);

	REQUIRE(files.size() == 5);
	REQUIRE(isInFileInfos(
		files,
		"./data/FileSystemTestSuite/src/Settings/player.h",
		"./data/FileSystemTestSuite/player.h"));
	REQUIRE(isInFileInfos(
		files,
		"./data/FileSystemTestSuite/src/Settings/sample.cpp",
		"./data/FileSystemTestSuite/sample.cpp"));
	REQUIRE(isInFileInfos(
		files,
		"./data/FileSystemTestSuite/src/main.cpp",
		"./data/FileSystemTestSuite/src/Settings/src/main.cpp"));
	REQUIRE(isInFileInfos(
		files,
		"./data/FileSystemTestSuite/src/test.cpp",
		"./data/FileSystemTestSuite/src/Settings/src/test.cpp"));
	REQUIRE(isInFileInfos(
		files,
		"./data/FileSystemTestSuite/src/test.h",
		"./data/FileSystemTestSuite/src/Settings/src/test.h"));
}

TEST_CASE("find symlinked directories")
{
	ASSERT_SYMLINK_PLATFORM();

	std::vector<FilePath> directoryPaths;
	directoryPaths.push_back(FilePath("./data/FileSystemTestSuite/src"));

	std::set<FilePath> dirs = FileSystem::getSymLinkedDirectories(directoryPaths);

	REQUIRE(dirs.size() == 2);
}

#if !defined(_WIN32)
TEST_CASE("file system answers for a symlink loop instead of throwing")
{
	const TestTempDirectory tempDirectory{"FileSystemTestSuite"};
	const std::filesystem::path loop = tempDirectory.getPath() / "loop";
	std::filesystem::create_symlink(loop.filename(), loop);
	const FilePath path(loop.string());

	REQUIRE(FileSystem::getFileByteSize(path) == 0);
	REQUIRE_FALSE(FileSystem::getLastWriteTime(path).isValid());
	REQUIRE_FALSE(FileSystem::getFileInfoForPath(path).path.exists());
	REQUIRE(FileSystem::getDirectSubDirectories(FilePath(tempDirectory.getPath().string())).empty());
}

TEST_CASE("file system reports a failed rename or copy instead of throwing")
{
	const TestTempDirectory tempDirectory{"FileSystemTestSuite"};
	const FilePath from(tempDirectory.getFilePath("from.txt"));
	std::ofstream(from.str()) << "x";
	// Into a directory that does not exist: the call fails, it does not throw.
	const FilePath to(tempDirectory.getFilePath("missing/to.txt"));

	REQUIRE_FALSE(FileSystem::rename(from, to));
	REQUIRE_FALSE(FileSystem::copyFile(from, to));
	REQUIRE(from.recheckExists());
}
#endif

#if !defined(_WIN32)
TEST_CASE("file system lists the files beside a directory it cannot read")
{
	// One `chmod 000` subfolder under a source path made the whole source group find 0 files: the
	// walk's failed step into it ended the walk. Whatever the listing order, the files of the other
	// directories are found now.
	const TestTempDirectory tempDirectory{"FileSystemTestSuite"};
	const std::filesystem::path root = tempDirectory.getPath();
	std::filesystem::create_directories(root / "unreadable" / "inner");
	std::ofstream(root / "unreadable" / "inner" / "hidden.cpp") << "";
	for (int i = 0; i < 20; ++i)
	{
		std::filesystem::create_directories(root / std::to_string(i));
		std::ofstream(root / std::to_string(i) / "file.cpp") << "";
	}
	std::filesystem::permissions(root / "unreadable", std::filesystem::perms::none);
	struct RestorePermissions	// so the temp directory can be removed
	{
		std::filesystem::path path;
		~RestorePermissions()
		{
			std::error_code ec;
			std::filesystem::permissions(path, std::filesystem::perms::owner_all, ec);
		}
	} restore{root / "unreadable"};

	const std::vector<FileInfo> infos =
		FileSystem::getFileInfosFromPaths({FilePath(root.string())}, {".cpp"});
	REQUIRE(infos.size() == 20);
	REQUIRE(isInFileInfos(infos, (root / "0" / "file.cpp").string()));
	REQUIRE(isInFileInfos(infos, (root / "19" / "file.cpp").string()));

	REQUIRE(FileSystem::getFilePathsFromDirectory(FilePath(root.string()), {".cpp"}).size() == 20);
}
#endif
