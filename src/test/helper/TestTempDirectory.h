#ifndef TEST_TEMP_DIRECTORY_H
#define TEST_TEMP_DIRECTORY_H

#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

// A fresh, uniquely named directory under the system temp directory, removed together with its
// contents on destruction. ctest runs every TEST_CASE as its own process and `ctest -j` runs those
// processes concurrently, so a test that writes files must not use a fixed path (e.g. under data/):
// parallel test processes would clobber each other's databases and files.
//
// Deliberately std-only (no FilePath): it is included by suites that `import` the srctrl modules,
// where the textual FilePath.h is guarded out. Construct a FilePath from getFilePath() instead.
class TestTempDirectory
{
public:
	explicit TestTempDirectory(std::string_view label)
		: m_path{createUniqueDirectory(label)}
	{
	}

	~TestTempDirectory()
	{
		std::error_code error;	  // never throw from a destructor; a leftover temp dir is harmless
		std::filesystem::remove_all(m_path, error);
	}

	TestTempDirectory(const TestTempDirectory&) = delete;
	TestTempDirectory& operator=(const TestTempDirectory&) = delete;

	const std::filesystem::path& getPath() const
	{
		return m_path;
	}

	// The path of `name` inside this directory, as the string FilePath is constructed from.
	std::string getFilePath(std::string_view name) const
	{
		return (m_path / name).string();
	}

private:
	static std::filesystem::path createUniqueDirectory(std::string_view label)
	{
		// Canonical, so paths compare equal to ones the code under test canonicalizes (on macOS
		// the temp directory lives behind the /var -> /private/var symlink).
		const std::filesystem::path root{
			std::filesystem::canonical(std::filesystem::temp_directory_path())};
		std::random_device random;
		while (true)
		{
			const std::filesystem::path path{
				root / ("sourcetrail_test_" + std::string{label} + "_" + std::to_string(random()) +
						std::to_string(random()))};
			// create_directory() returns false if the directory already exists: just draw again.
			if (std::filesystem::create_directory(path))
				return path;
		}
	}

	const std::filesystem::path m_path;
};

#endif	  // TEST_TEMP_DIRECTORY_H
