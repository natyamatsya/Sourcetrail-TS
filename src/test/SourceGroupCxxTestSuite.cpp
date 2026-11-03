#include "Catch2.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>

#ifndef SRCTRL_MODULE_BUILD
#include "AppPath.h"
#endif
#include "Application.h"
#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#include "IndexerCommand.h"
#include "IndexerCommandCxx.h"
#endif
#include "Platform.h"
#ifndef SRCTRL_MODULE_BUILD
#include "ProjectSettings.h"
#endif
#include "CMakeFileAPIReader.h"
#include "SourceGroupCxxCMakeFileAPI.h"
#include "SourceGroupCxxCdb.h"
#include "SourceGroupCxxEmpty.h"
#include "SourceGroupCxxMake.h"
#include "SourceGroupFactory.h"
#include "SourceGroupFactoryModuleCxx.h"
#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettingsCEmpty.h"
#include "SourceGroupSettingsCppEmpty.h"
#include "SourceGroupSettingsCxxCMakeFileAPI.h"
#include "SourceGroupSettingsCxxCdb.h"
#include "SourceGroupSettingsCxxMake.h"
#include "TextAccess.h"
#endif
#include "TestTempDirectory.h"
#include "Version.h"

#include <llvm/Config/llvm-config.h>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.cxx;
import srctrl.file;
import srctrl.indexer;
import srctrl.settings;
#endif

using namespace std;
using namespace utility;

namespace
{
const bool updateExpectedOutput = false;

class SharedDataDirectorySwitcher
{
public:
	SharedDataDirectorySwitcher(const FilePath& newDirectory)
	{
		m_oldDirectory = AppPath::getSharedDataDirectoryPath();
		AppPath::setSharedDataDirectoryPath(newDirectory);
	}

	~SharedDataDirectorySwitcher()
	{
		AppPath::setSharedDataDirectoryPath(m_oldDirectory);
	}

private:
	FilePath m_oldDirectory;
};

class ScopedApplicationInstance
{
public:
	ScopedApplicationInstance()
	{
		Application::createInstance(Version(), nullptr, nullptr);
	}

	~ScopedApplicationInstance()
	{
		Application::destroyInstance();
	}
};

FilePath getInputDirectoryPath(const std::string& projectName)
{
	return FilePath("data/SourceGroupTestSuite/" + projectName + "/input")
		.makeAbsolute()
		.makeCanonical();
}

FilePath getOutputDirectoryPath(const std::string& projectName)
{
	return FilePath("data/SourceGroupTestSuite/" + projectName + "/expected_output")
		.makeAbsolute()
		.makeCanonical();
}

std::string indexerCommandCxxToString(
	const IndexerCommandCxx& indexerCommand,
	const FilePath& baseDirectory)
{
	std::string result;
	result += "SourceFilePath: \"" +
		indexerCommand.getSourceFilePath().getRelativeTo(baseDirectory).str() + "\"\n";
	for (const FilePath& indexedPath: indexerCommand.getIndexedPaths())
		result += "\tIndexedPath: \"" + indexedPath.getRelativeTo(baseDirectory).str() + "\"\n";
	bool previousFlagWasSysroot = false;
	for (std::string compilerFlag: indexerCommand.getCompilerFlags())
	{
		if (previousFlagWasSysroot)
		{
			// The SDK path is machine-specific (local Xcode installation).
			result += "\tCompilerFlag: \"<sysroot>\"\n";
			previousFlagWasSysroot = false;
			continue;
		}
		previousFlagWasSysroot = compilerFlag == "-isysroot";

		FilePath flagAsPath(compilerFlag);
		if (flagAsPath.exists())
			compilerFlag = flagAsPath.getRelativeTo(baseDirectory).str();
		result += "\tCompilerFlag: \"" + compilerFlag + "\"\n";
	}
	for (const FilePathFilter& filter: indexerCommand.getExcludeFilters())
		result += "\tExcludeFilter: \"" + filter.str() + "\"\n";
	return result;
}

std::string indexerCommandToString(
	std::shared_ptr<IndexerCommand> indexerCommand,
	const FilePath& baseDirectory)
{
	if (!indexerCommand)
		return "No IndexerCommand provided.";

	if (const auto indexerCommandCxx = indexerCommand->target<IndexerCommandCxx>())
		return indexerCommandCxxToString(*indexerCommandCxx, baseDirectory);

	return "Unsupported indexer command type: " +
		indexerCommandTypeToString(indexerCommand->getIndexerCommandType());
}

std::shared_ptr<TextAccess> generateExpectedOutput(
	const std::string& projectName,
	std::shared_ptr<const SourceGroup> sourceGroup)
{
	const FilePath projectDataRoot = getInputDirectoryPath(projectName).makeAbsolute();

	RefreshInfo info;
	info.filesToIndex = sourceGroup->getAllSourceFilePaths();
	std::vector<std::shared_ptr<IndexerCommand>> indexerCommands = sourceGroup->getIndexerCommands(
		info);

	std::sort(
		indexerCommands.begin(),
		indexerCommands.end(),
		[](std::shared_ptr<IndexerCommand> a, std::shared_ptr<IndexerCommand> b) {
			return a->getSourceFilePath().str() < b->getSourceFilePath().str();
		});

	std::string outputString;
	for (const std::shared_ptr<IndexerCommand>& indexerCommand: indexerCommands)
		outputString += indexerCommandToString(indexerCommand, projectDataRoot);

	return TextAccess::createFromString(outputString);
}

std::string getOutputFilename()
{
	if constexpr (Platform::isWindows())
		return "output_windows.txt";
	else if constexpr (Platform::isMac())
		// macOS output differs from Linux: the source groups inject the local
		// SDK via "-isysroot" (sanitized to "<sysroot>" above).
		return "output_macos.txt";
	else
		return "output_unix.txt";
}

void generateAndCompareExpectedOutput(
	const std::string& projectName,
	std::shared_ptr<const SourceGroup> sourceGroup)
{
	const std::shared_ptr<const TextAccess> output = generateExpectedOutput(projectName, sourceGroup);
	const std::string expectedOutputFileName = getOutputFilename();

	const FilePath expectedOutputFilePath =
		getOutputDirectoryPath(projectName).concatenate(expectedOutputFileName);
	if (updateExpectedOutput || !expectedOutputFilePath.exists())
	{
		std::ofstream expectedOutputFile;
		expectedOutputFile.open(expectedOutputFilePath.str());
		expectedOutputFile << output->getText();
		expectedOutputFile.close();
		return;
	}

	const std::shared_ptr<const TextAccess> expectedOutput = TextAccess::createFromFile(
		expectedOutputFilePath);
	REQUIRE_MESSAGE(
		("Output does not match the expected line count for project \"" + projectName) +
		"\". Output was: " + output->getText(),
		expectedOutput->getLineCount() == output->getLineCount());
	if (expectedOutput->getLineCount() != output->getLineCount())
		return;

	for (unsigned int i = 1; i <= expectedOutput->getLineCount(); i++)
		REQUIRE(expectedOutput->getLine(i) == output->getLine(i));
}
}  // namespace

TEST_CASE("source group cxx c empty generates expected output")
{
	ScopedApplicationInstance scopedApplicationInstance;
	SharedDataDirectorySwitcher sharedDataDirectorySwitcher((FilePath()));

	const std::string projectName = "cxx_c_empty";

	ProjectSettings projectSettings;
	projectSettings.setProjectFilePath("non_existent_project", getInputDirectoryPath(projectName));

	std::shared_ptr<SourceGroupSettingsCEmpty> sourceGroupSettings =
		std::make_shared<SourceGroupSettingsCEmpty>("fake_id", &projectSettings);
	sourceGroupSettings->setSourcePaths({getInputDirectoryPath(projectName).concatenate("src")});
	sourceGroupSettings->setSourceExtensions({".c"});
	sourceGroupSettings->setExcludeFilterStrings({"**/excluded/**"});
	sourceGroupSettings->setTargetOptionsEnabled(true);
	sourceGroupSettings->setTargetArch("test_arch");
	sourceGroupSettings->setTargetVendor("test_vendor");
	sourceGroupSettings->setTargetSys("test_sys");
	sourceGroupSettings->setTargetAbi("test_abi");
	sourceGroupSettings->setCStandard("c11");
	sourceGroupSettings->setHeaderSearchPaths(
		{getInputDirectoryPath(projectName).concatenate("header_search/local")});
	sourceGroupSettings->setFrameworkSearchPaths(
		{getInputDirectoryPath(projectName).concatenate("framework_search/local")});
	sourceGroupSettings->setCompilerFlags({"-local-flag"});

	std::shared_ptr<ApplicationSettings> applicationSettings = ApplicationSettings::getInstance();

	std::vector<FilePath> storedHeaderSearchPaths = applicationSettings->getHeaderSearchPaths();
	std::vector<FilePath> storedFrameworkSearchPaths = applicationSettings->getFrameworkSearchPaths();

	applicationSettings->setHeaderSearchPaths({FilePath("test/header/search/path")});
	applicationSettings->setFrameworkSearchPaths({FilePath("test/framework/search/path")});

	generateAndCompareExpectedOutput(
		projectName, std::make_shared<SourceGroupCxxEmpty>(sourceGroupSettings));

	applicationSettings->setHeaderSearchPaths(storedHeaderSearchPaths);
	applicationSettings->setFrameworkSearchPaths(storedFrameworkSearchPaths);
}

TEST_CASE("source group cxx cpp empty generates expected output")
{
	ScopedApplicationInstance scopedApplicationInstance;
	SharedDataDirectorySwitcher sharedDataDirectorySwitcher((FilePath()));

	const std::string projectName = "cxx_cpp_empty";

	ProjectSettings projectSettings;
	projectSettings.setProjectFilePath("non_existent_project", getInputDirectoryPath(projectName));

	std::shared_ptr<SourceGroupSettingsCppEmpty> sourceGroupSettings =
		std::make_shared<SourceGroupSettingsCppEmpty>("fake_id", &projectSettings);
	sourceGroupSettings->setSourcePaths({getInputDirectoryPath(projectName).concatenate("/src")});
	sourceGroupSettings->setSourceExtensions({".cpp"});
	sourceGroupSettings->setExcludeFilterStrings({"**/excluded/**"});
	sourceGroupSettings->setTargetOptionsEnabled(true);
	sourceGroupSettings->setTargetArch("test_arch");
	sourceGroupSettings->setTargetVendor("test_vendor");
	sourceGroupSettings->setTargetSys("test_sys");
	sourceGroupSettings->setTargetAbi("test_abi");
	sourceGroupSettings->setCppStandard("c++11");
	sourceGroupSettings->setHeaderSearchPaths(
		{getInputDirectoryPath(projectName).concatenate("header_search/local")});
	sourceGroupSettings->setFrameworkSearchPaths(
		{getInputDirectoryPath(projectName).concatenate("framework_search/local")});
	sourceGroupSettings->setCompilerFlags({"-local-flag"});

	std::shared_ptr<ApplicationSettings> applicationSettings = ApplicationSettings::getInstance();

	std::vector<FilePath> storedHeaderSearchPaths = applicationSettings->getHeaderSearchPaths();
	std::vector<FilePath> storedFrameworkSearchPaths = applicationSettings->getFrameworkSearchPaths();

	applicationSettings->setHeaderSearchPaths({FilePath("test/header/search/path")});
	applicationSettings->setFrameworkSearchPaths({FilePath("test/framework/search/path")});

	generateAndCompareExpectedOutput(
		projectName, std::make_shared<SourceGroupCxxEmpty>(sourceGroupSettings));

	applicationSettings->setHeaderSearchPaths(storedHeaderSearchPaths);
	applicationSettings->setFrameworkSearchPaths(storedFrameworkSearchPaths);
}

TEST_CASE("source group cxx cdb generates expected output")
{
	ScopedApplicationInstance scopedApplicationInstance;
	SharedDataDirectorySwitcher sharedDataDirectorySwitcher((FilePath()));

	const std::string projectName = "cxx_cdb";

	ProjectSettings projectSettings;
	projectSettings.setProjectFilePath("non_existent_project", getInputDirectoryPath(projectName));

	std::shared_ptr<SourceGroupSettingsCxxCdb> sourceGroupSettings =
		std::make_shared<SourceGroupSettingsCxxCdb>("fake_id", &projectSettings);
	sourceGroupSettings->setIndexedHeaderPaths({FilePath("test/indexed/header/path")});
	sourceGroupSettings->setCompilationDatabasePath(
		getInputDirectoryPath(projectName).concatenate("compile_commands.json"));
	sourceGroupSettings->setExcludeFilterStrings({"**/excluded/**"});
	sourceGroupSettings->setHeaderSearchPaths(
		{getInputDirectoryPath(projectName).concatenate("header_search/local")});
	sourceGroupSettings->setFrameworkSearchPaths(
		{getInputDirectoryPath(projectName).concatenate("framework_search/local")});
	sourceGroupSettings->setCompilerFlags({"-local-flag"});

	std::shared_ptr<ApplicationSettings> applicationSettings = ApplicationSettings::getInstance();

	std::vector<FilePath> storedHeaderSearchPaths = applicationSettings->getHeaderSearchPaths();
	std::vector<FilePath> storedFrameworkSearchPaths = applicationSettings->getFrameworkSearchPaths();

	applicationSettings->setHeaderSearchPaths({FilePath("test/header/search/path")});
	applicationSettings->setFrameworkSearchPaths({FilePath("test/framework/search/path")});

	generateAndCompareExpectedOutput(
		projectName, std::make_shared<SourceGroupCxxCdb>(sourceGroupSettings));

	applicationSettings->setHeaderSearchPaths(storedHeaderSearchPaths);
	applicationSettings->setFrameworkSearchPaths(storedFrameworkSearchPaths);
}

TEST_CASE("source gropup cxx c correct default standard")
{
	const std::string defaultCStandard = SourceGroupSettingsWithCStandard::getDefaultCStandard();
	const std::string defaultCppStandard = SourceGroupSettingsWithCppStandard::getDefaultCppStandard();

	// The newest released standard; C23 counts as released from clang 20 on.
#if LLVM_VERSION_MAJOR >= 20
	REQUIRE(defaultCStandard == "c23");
#else
	REQUIRE(defaultCStandard == "c17");
#endif
	REQUIRE(defaultCppStandard == "c++23");
}

TEST_CASE("source group cxx cdb prebuilds the modules a partial refresh's files import")
{
	ScopedApplicationInstance scopedApplicationInstance;

	const std::filesystem::path directory =
		std::filesystem::temp_directory_path() / "sourcetrail-module-tests" / "cdb-partial-refresh";
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory);
	std::ofstream(directory / "geo.cppm") << "export module geo;\nexport int area(int w, int h) { return w * h; }\n";
	std::ofstream(directory / "main.cpp") << "import geo;\nint main() { return area(2, 3); }\n";
	std::ofstream(directory / "compile_commands.json") << std::format(
		R"([{{"directory": "{0}", "file": "geo.cppm", "arguments": ["clang++", "-std=c++20", "-c", "geo.cppm"]}},
		    {{"directory": "{0}", "file": "main.cpp", "arguments": ["clang++", "-std=c++20", "-c", "main.cpp"]}}])",
		directory.generic_string());	// forward slashes: Windows' backslashes are escapes in JSON

	ProjectSettings projectSettings;
	projectSettings.setProjectFilePath("modules", FilePath(directory.string()));
	std::shared_ptr<SourceGroupSettingsCxxCdb> sourceGroupSettings =
		std::make_shared<SourceGroupSettingsCxxCdb>("fake_id", &projectSettings);
	sourceGroupSettings->setCompilationDatabasePath(
		FilePath((directory / "compile_commands.json").string()));
	const SourceGroupCxxCdb sourceGroup(sourceGroupSettings);
	const std::filesystem::path cache = directory / "module_cache";

	SECTION("an importer refreshed on its own still finds the BMI of the unchanged module")
	{
		RefreshInfo info;
		info.filesToIndex = {FilePath((directory / "main.cpp").string()).makeCanonical()};
		const std::vector<std::shared_ptr<IndexerCommand>> commands = sourceGroup.getIndexerCommands(info);

		REQUIRE(commands.size() == 1);
		const auto command = commands.front()->target<IndexerCommandCxx>();
		REQUIRE(command.has_value());
		REQUIRE(std::ranges::contains(
			command->getCompilerFlags(), "-fprebuilt-module-path=" + FilePath(cache.string()).str()));
		REQUIRE(std::filesystem::exists(cache / "geo.pcm"));
	}

	SECTION("command hashes are computed without building any BMI")
	{
		REQUIRE(sourceGroup.getAllSourceFileCommandHashes().size() == 2);
		REQUIRE_FALSE(std::filesystem::exists(cache));
	}
}

TEST_CASE("source group cxx cdb hashes a file compiled twice once, the same each time")
{
	ScopedApplicationInstance scopedApplicationInstance;

	// A library's objects compiled for its static and its shared target.
	const std::filesystem::path directory =
		std::filesystem::temp_directory_path() / "sourcetrail-cdb-tests" / "compiled-twice";
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory);
	std::ofstream(directory / "lib.cpp") << "int f() { return 1; }\n";
	const auto writeDatabase = [&](const std::string& firstFlag, const std::string& secondFlag) {
		std::ofstream(directory / "compile_commands.json") << std::format(
			R"([{{"directory": "{0}", "file": "lib.cpp", "arguments": ["clang++", "{1}", "-c", "lib.cpp"]}},
			    {{"directory": "{0}", "file": "lib.cpp", "arguments": ["clang++", "{2}", "-c", "lib.cpp"]}}])",
			directory.generic_string(),
			firstFlag,
			secondFlag);
	};

	ProjectSettings projectSettings;
	projectSettings.setProjectFilePath("twice", FilePath(directory.string()));
	std::shared_ptr<SourceGroupSettingsCxxCdb> sourceGroupSettings =
		std::make_shared<SourceGroupSettingsCxxCdb>("fake_id", &projectSettings);
	sourceGroupSettings->setCompilationDatabasePath(FilePath((directory / "compile_commands.json").string()));

	writeDatabase("-DSTATIC", "-fPIC");
	const auto hashes = SourceGroupCxxCdb(sourceGroupSettings).getAllSourceFileCommandHashes();
	REQUIRE(hashes.size() == 1);
	// Each rewrite below keeps the size, and gets the first one's modification time back, as a
	// rewrite within one timestamp tick does (on Windows it did): the loaded database's cache
	// kept the old commands for it.
	const std::filesystem::file_time_type firstWritten =
		std::filesystem::last_write_time(directory / "compile_commands.json");
	const auto rewriteDatabase = [&](const std::string& firstFlag, const std::string& secondFlag) {
		writeDatabase(firstFlag, secondFlag);
		std::filesystem::last_write_time(directory / "compile_commands.json", firstWritten);
	};

	// The commands' order does not matter; a changed command does.
	rewriteDatabase("-fPIC", "-DSTATIC");
	REQUIRE(SourceGroupCxxCdb(sourceGroupSettings).getAllSourceFileCommandHashes() == hashes);
	rewriteDatabase("-fPIC", "-DSHARED");
	const auto changed = SourceGroupCxxCdb(sourceGroupSettings).getAllSourceFileCommandHashes();
	REQUIRE(changed.size() == 1);
	REQUIRE(changed.front().second != hashes.front().second);
}

namespace
{
// Writes `content` as the compile_commands.json of `directory` and checks it as the source group does.
std::expected<void, CdbLoadError> checkDatabase(const TestTempDirectory& directory, const std::string& content)
{
	std::ofstream(directory.getPath() / "compile_commands.json") << content;
	return SourceGroupCxxCdb::checkCompilationDatabase(FilePath(directory.getFilePath("compile_commands.json")));
}
}	 // namespace

TEST_CASE("source group cxx cdb refuses a compilation database it cannot index, naming the problem")
{
	// Each was accepted without a word: "0 source files indexed", exit code 0.
	const TestTempDirectory directory{"SourceGroupCxxTestSuite"};
	const std::string dir = directory.getPath().generic_string();
	std::ofstream(directory.getPath() / "main.cpp") << "int main() {}\n";

	const auto requireError = [&](const std::string& content, CdbLoadError::Code code, const std::string& says) {
		const auto checked = checkDatabase(directory, content);
		INFO(content);
		REQUIRE_FALSE(checked.has_value());
		INFO(describe(checked.error()));
		REQUIRE(checked.error().code == code);
		REQUIRE(describe(checked.error()).find(says) != std::string::npos);
	};

	using enum CdbLoadError::Code;
	requireError("", ParseFailed, "is empty");
	requireError("[{\"directory\": ", ParseFailed, "cannot be read");
	requireError("{}", ParseFailed, "Expected array");
	requireError("[]", NoCompileCommands, "has no compile commands");
	requireError("[1,2,3]", ParseFailed, "Expected object");
	requireError("[null, {}, \"str\"]", ParseFailed, "Expected object");
	requireError(R"([{"directory": ["x"], "file": {"a":1}, "arguments": [1, null, true]}])", ParseFailed, "Expected");
	requireError(
		std::format(R"([{{"directory": "{}", "arguments": ["clang++", "-c", "main.cpp"]}}])", dir),
		ParseFailed,
		"Missing key: \"file\"");
	requireError(
		std::format(R"([{{"file": "{}/main.cpp", "arguments": ["clang++", "-c", "main.cpp"]}}])", dir),
		ParseFailed,
		"Missing key: \"directory\"");
	requireError(
		std::format(R"([{{"directory": "{0}", "file": "{0}/main.cpp", "arguments": "clang++ -c main.cpp"}}])", dir),
		ParseFailed,
		"Expected sequence");
	requireError(
		std::format(R"([{{"directory": "{0}", "file": "{0}/nope.cpp", "arguments": ["clang++", "-c", "nope.cpp"]}}])", dir),
		NoSourceFiles,
		"nope.cpp");

	std::filesystem::create_directory(directory.getPath() / "is_a_directory.json");
	const auto directoryChecked =
		SourceGroupCxxCdb::checkCompilationDatabase(FilePath(directory.getFilePath("is_a_directory.json")));
	REQUIRE_FALSE(directoryChecked.has_value());
	REQUIRE(directoryChecked.error().code == NotAFile);

	const auto missingChecked =
		SourceGroupCxxCdb::checkCompilationDatabase(FilePath(directory.getFilePath("missing.json")));
	REQUIRE_FALSE(missingChecked.has_value());
	REQUIRE(missingChecked.error().code == PathMissing);

	// One file there among missing ones is a database to index (the missing ones are logged).
	REQUIRE(checkDatabase(
		directory,
		std::format(
			R"([{{"directory": "{0}", "file": "nope.cpp", "arguments": ["clang++", "-c", "nope.cpp"]}},
			    {{"directory": "{0}", "file": "main.cpp", "arguments": ["clang++", "-c", "main.cpp"]}}])",
			dir)));
}

TEST_CASE("source group cxx cdb indexes a file whose command's directory is gone, or relative")
{
	// "nodir/../main.cpp" named no file once its directory was gone: the file was dropped without a
	// word. Now it is indexed, and the indexer reports the missing working directory as its error.
	ScopedApplicationInstance scopedApplicationInstance;
	const TestTempDirectory directory{"SourceGroupCxxTestSuite"};
	std::ofstream(directory.getPath() / "main.cpp") << "int main() {}\n";
	const std::string dir = directory.getPath().generic_string();

	ProjectSettings projectSettings;
	projectSettings.setProjectFilePath("gone", FilePath(directory.getPath().string()));
	std::shared_ptr<SourceGroupSettingsCxxCdb> sourceGroupSettings =
		std::make_shared<SourceGroupSettingsCxxCdb>("fake_id", &projectSettings);
	sourceGroupSettings->setCompilationDatabasePath(FilePath(directory.getFilePath("compile_commands.json")));
	const FilePath mainFile = FilePath(directory.getFilePath("main.cpp")).makeCanonical();

	const auto commandFor = [&](const std::string& content) {
		std::ofstream(directory.getPath() / "compile_commands.json") << content;
		const SourceGroupCxxCdb sourceGroup(sourceGroupSettings);
		RefreshInfo info;
		info.filesToIndex = sourceGroup.getAllSourceFilePaths();
		const std::vector<std::shared_ptr<IndexerCommand>> commands = sourceGroup.getIndexerCommands(info);
		REQUIRE(commands.size() == 1);
		REQUIRE(commands.front()->getSourceFilePath() == mainFile);
		return commands.front()->target<IndexerCommandCxx>().value();
	};

	SECTION("a directory that is gone is kept, for the indexer to report")
	{
		const IndexerCommandCxx command = commandFor(std::format(
			R"([{{"directory": "{}/nodir", "file": "../main.cpp", "arguments": ["clang++", "-c", "../main.cpp"]}}])",
			dir));
		REQUIRE(command.getWorkingDirectory().str() == dir + "/nodir");
	}

	SECTION("a relative directory is the database's")
	{
		const IndexerCommandCxx command = commandFor(
			R"([{"directory": ".", "file": "main.cpp", "arguments": ["clang++", "-c", "main.cpp"]}])");
		REQUIRE(command.getWorkingDirectory().str() == FilePath(dir).str());
	}
}

TEST_CASE("source group cxx cdb indexes identical compile commands once, distinct ones each")
{
	// 100 copies of one command indexed the file 100 times, and 200,000 effectively hung.
	ScopedApplicationInstance scopedApplicationInstance;
	const TestTempDirectory directory{"SourceGroupCxxTestSuite"};
	std::ofstream(directory.getPath() / "main.cpp") << "int main() {}\n";
	const std::string command = std::format(
		R"({{"directory": "{}", "file": "main.cpp", "arguments": ["clang++", "-c", "main.cpp"]}})",
		directory.getPath().generic_string());
	const std::string otherConfiguration = std::format(
		R"({{"directory": "{}", "file": "main.cpp", "arguments": ["clang++", "-DOTHER", "-c", "main.cpp"]}})",
		directory.getPath().generic_string());
	std::ofstream(directory.getPath() / "compile_commands.json")
		<< "[" << command << "," << command << "," << otherConfiguration << "," << command << "]";

	ProjectSettings projectSettings;
	projectSettings.setProjectFilePath("dups", FilePath(directory.getPath().string()));
	std::shared_ptr<SourceGroupSettingsCxxCdb> sourceGroupSettings =
		std::make_shared<SourceGroupSettingsCxxCdb>("fake_id", &projectSettings);
	sourceGroupSettings->setCompilationDatabasePath(FilePath(directory.getFilePath("compile_commands.json")));
	const SourceGroupCxxCdb sourceGroup(sourceGroupSettings);

	RefreshInfo info;
	info.filesToIndex = sourceGroup.getAllSourceFilePaths();
	const std::vector<std::shared_ptr<IndexerCommand>> commands = sourceGroup.getIndexerCommands(info);

	REQUIRE(commands.size() == 2);
	const auto flagsOf = [&](std::size_t i) { return commands[i]->target<IndexerCommandCxx>()->getCompilerFlags(); };
	REQUIRE(std::ranges::contains(flagsOf(0), "-DOTHER") != std::ranges::contains(flagsOf(1), "-DOTHER"));
}

TEST_CASE("source group factory creates a source group configured twice once")
{
	// 100 identical source groups indexed every file 100 times.
	SourceGroupFactory::getInstance()->addModule(std::make_shared<SourceGroupFactoryModuleCxx>());
	ProjectSettings projectSettings;
	projectSettings.setProjectFilePath("twice", FilePath("/tmp"));
	const auto cdbGroup = [&](const std::string& id, const std::string& name, const std::string& cdbPath) {
		std::shared_ptr<SourceGroupSettingsCxxCdb> settings =
			std::make_shared<SourceGroupSettingsCxxCdb>(id, &projectSettings);
		settings->setName(name);
		settings->setCompilationDatabasePath(FilePath(cdbPath));
		return std::shared_ptr<SourceGroupSettings>(settings);
	};

	const std::vector<std::shared_ptr<SourceGroup>> groups = SourceGroupFactory::getInstance()->createSourceGroups(
		{cdbGroup("g0", "cdb", "/tmp/a/compile_commands.json"),
		 cdbGroup("g1", "cdb copy", "/tmp/a/compile_commands.json"),
		 cdbGroup("g2", "cdb", "/tmp/b/compile_commands.json"),
		 cdbGroup("g3", "cdb", "/tmp/a/compile_commands.json")});

	REQUIRE(groups.size() == 2);
	REQUIRE(groups[0]->getSourceGroupId() == "g0");
	REQUIRE(groups[1]->getSourceGroupId() == "g2");
}

TEST_CASE("source group cxx cmake configures afresh a build directory copied with its project")
{
	// A project copied with its *.srctrl-build directory and its .srctrl.toml pointed at the copy
	// indexed the old checkout's files: the File API reply holds absolute paths. Now the reply's
	// source directory is checked, and a reply written for another location is configured afresh.
	if (std::system("cmake --version > /dev/null 2>&1") != 0)
		SKIP("cmake is not on PATH");
#if defined(_WIN32)
	SKIP("POSIX shell redirection above");
#endif

	ScopedApplicationInstance scopedApplicationInstance;
	const TestTempDirectory tempDirectory{"SourceGroupCxxCMakeMoved"};
	const std::filesystem::path original = tempDirectory.getPath() / "original";
	std::filesystem::create_directories(original / "source");
	std::ofstream(original / "source" / "CMakeLists.txt")
		<< "cmake_minimum_required(VERSION 3.21)\nproject(probe NONE)\nadd_custom_target(only)\n";
	CMakeFileAPIReader originalReader{FilePath((original / "build").string())};
	REQUIRE(originalReader.ensureReply({}, FilePath((original / "source").string())).has_value());

	const std::filesystem::path copy = tempDirectory.getPath() / "copy";
	std::filesystem::copy(original, copy, std::filesystem::copy_options::recursive);
	const FilePath copySource((copy / "source").string());
	const FilePath copyBuild((copy / "build").string());
	REQUIRE(CMakeFileAPIReader{copyBuild}.getReplyPaths()->source == FilePath((original / "source").string()));

	ProjectSettings projectSettings;
	projectSettings.setProjectFilePath("copy", FilePath(copy.string()));
	std::shared_ptr<SourceGroupSettingsCxxCMakeFileAPI> settings =
		std::make_shared<SourceGroupSettingsCxxCMakeFileAPI>("fake_id", &projectSettings);
	settings->setSourceDirectory(copySource);
	settings->setBuildDirectory(copyBuild);
	SourceGroupCxxCMakeFileAPI sourceGroup(settings);

	REQUIRE(sourceGroup.prepareIndexing().has_value());
	const auto paths = CMakeFileAPIReader{copyBuild}.getReplyPaths();
	REQUIRE(paths.has_value());
	REQUIRE(paths->source == copySource);
	REQUIRE(paths->build == copyBuild);
}

TEST_CASE("a Makefile group's settings read back without a project file are the same")
{
	// The index stores the project's settings and reads them back without a project file to tell
	// whether they changed: the database a Makefile group writes is where the project is, so it
	// differed, and every load asked to reindex.
	const std::string text =
		"version = '8'\n\n[[source_groups]]\nid = 'make'\nname = 'Make'\nstatus = 'enabled'\n"
		"type = 'C/C++ from Makefile'\n\n    [source_groups.make]\n    arguments = 'all'\n"
		"    directory = '/src/game'\n    program = ''\n";
	const TestTempDirectory directory{"SourceGroupCxxMakeEquals"};
	std::ofstream(directory.getPath() / "game.srctrl.toml") << text;

	ProjectSettings project;
	REQUIRE(project.load(FilePath((directory.getPath() / "game.srctrl.toml").string())));
	ProjectSettings stored;
	REQUIRE(stored.loadFromString(text));

	REQUIRE(project.equalsExceptNameAndLocation(stored));

	std::shared_ptr<SourceGroupSettingsCxxMake> make =
		std::dynamic_pointer_cast<SourceGroupSettingsCxxMake>(project.getAllSourceGroupSettings().front());
	REQUIRE(make);
	make->setMakeArguments("all config=debug");
	project.setAllSourceGroupSettings({make});
	REQUIRE_FALSE(project.equalsExceptNameAndLocation(stored));
}

TEST_CASE("a Makefile group indexes the files its dry run compiles")
{
	ScopedApplicationInstance scopedApplicationInstance;
	const TestTempDirectory directory{"SourceGroupCxxMake"};
	const std::filesystem::path source = directory.getPath() / "game";
	std::filesystem::create_directories(source / "src");
	std::ofstream(source / "src" / "main.c") << "int main(void) { return 0; }\n";
	std::ofstream(source / "src" / "util.c") << "int util(void) { return 1; }\n";
	std::ofstream(source / "Makefile")
		<< "all: obj/main.o obj/util.o\n"
		   "obj/%.o: src/%.c\n\tmkdir -p obj\n\tcc -MMD -MP -Isrc -o $@ -c $<\n";

	ProjectSettings projectSettings;
	projectSettings.setProjectFilePath("game", FilePath((directory.getPath() / "project").string()));
	std::shared_ptr<SourceGroupSettingsCxxMake> settings =
		std::make_shared<SourceGroupSettingsCxxMake>("make_id", &projectSettings);
	settings->setMakefileDirectory(FilePath(source.string()));
	settings->setMakeArguments("all");

	const auto written = SourceGroupCxxMake::writeCompilationDatabase(*settings);
	if (!written && written.error().code == make::Error::Code::MakeNotFound)
	{
		SKIP("make is not installed");
	}
	REQUIRE(written.has_value());
	REQUIRE(written->commands.size() == 2);
	REQUIRE(settings->getCompilationDatabasePath() == settings->getGeneratedCompilationDatabasePath());

	const SourceGroupCxxMake sourceGroup(settings);
	const std::set<FilePath> files = sourceGroup.getAllSourceFilePaths();
	REQUIRE(files.size() == 2);
	REQUIRE(files.contains(FilePath((source / "src" / "main.c").string()).makeCanonical()));

	// Nothing to compile: why, and what make was given.
	settings->setMakeArguments("nosuchgoal");
	const auto nothing = SourceGroupCxxMake::writeCompilationDatabase(*settings);
	REQUIRE_FALSE(nothing.has_value());
	REQUIRE(nothing.error().code == make::Error::Code::NoCompileCommands);
	REQUIRE(nothing.error().message.contains("nosuchgoal"));
}
