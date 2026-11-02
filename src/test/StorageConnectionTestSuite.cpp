#include "Catch2.hpp"

#include <filesystem>
#include <string>

#include "StorageConnection.h"
#include "TestTempDirectory.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
#endif

namespace
{
// What opening the database at `path` throws; empty when it opens.
std::string openFailure(const FilePath& path)
{
	try
	{
		const StorageConnection connection(path);
	}
	catch (CppSQLite3Exception& e)
	{
		return e.errorMessage();
	}
	return {};
}
}	 // namespace

#if !defined(_WIN32)
TEST_CASE("a database path that is a symlink loop fails to open and says why")
{
	// `ln -s X.srctrl.db X.srctrl.db`: opening the project terminated the app (std::terminate in
	// FilePath::exists()); now the open fails, naming the loop.
	const TestTempDirectory tempDirectory{"StorageConnectionTestSuite"};
	const std::filesystem::path loop = tempDirectory.getPath() / "loop.srctrl.db";
	std::filesystem::create_symlink(loop.filename(), loop);

	const std::string failure = openFailure(FilePath(loop.string()));

	REQUIRE(failure.find("SQLITE_CANTOPEN") != std::string::npos);
	REQUIRE(failure.find(loop.string()) != std::string::npos);
	REQUIRE(failure.find(std::make_error_code(std::errc::too_many_symbolic_link_levels).message()) !=
			std::string::npos);
}
#endif

TEST_CASE("a database path that is a directory fails to open and says why")
{
	const TestTempDirectory tempDirectory{"StorageConnectionTestSuite"};
	std::filesystem::create_directory(tempDirectory.getPath() / "dir.srctrl.db");

	const std::string failure = openFailure(FilePath(tempDirectory.getFilePath("dir.srctrl.db")));

	REQUIRE(failure.find("it is a directory") != std::string::npos);
}

TEST_CASE("a database path that does not exist yet opens")
{
	const TestTempDirectory tempDirectory{"StorageConnectionTestSuite"};

	REQUIRE(openFailure(FilePath(tempDirectory.getFilePath("new.srctrl.db"))).empty());
}
