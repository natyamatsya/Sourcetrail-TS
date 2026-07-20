#include "OptionalCxxTestUtils.h"

#include "Catch2.hpp"

#include "IndexerCommandSerializer.h"

#if BUILD_CXX_LANGUAGE_PACKAGE
#include "IndexerCommand.h"
#include "IndexerCommandCxx.h"
#endif

std::shared_ptr<IndexerCommand> makeOptionalCxxCommand(
	const std::string& sourceFilePath,
	const std::set<FilePath>& indexedPaths,
	const std::string& workingDirectory,
	const std::vector<std::string>& compilerFlags)
{
#if BUILD_CXX_LANGUAGE_PACKAGE
	return std::make_shared<IndexerCommand>(
		FilePath(sourceFilePath),
		IndexerCommandCxx(
		FilePath(sourceFilePath),
		indexedPaths,
		std::set<FilePathFilter>{},
		std::set<FilePathFilter>{},
		FilePath(workingDirectory),
		compilerFlags,
		std::string{}));
#else
	(void)sourceFilePath;
	(void)indexedPaths;
	(void)workingDirectory;
	(void)compilerFlags;
	return nullptr;
#endif
}

bool isOptionalCxxCommand(const std::shared_ptr<IndexerCommand>& command)
{
#if BUILD_CXX_LANGUAGE_PACKAGE
	if (!command)
		return false;

	return command->getIndexerCommandType() == IndexerCommandType::INDEXER_COMMAND_CXX;
#else
	(void)command;
	return false;
#endif
}

void assertOptionalCxxSerializerRoundTrip()
{
#if BUILD_CXX_LANGUAGE_PACKAGE
	std::set<FilePath> indexedPaths = {FilePath("/usr/include"), FilePath("/opt/include")};
	std::set<FilePathFilter> excludeFilters = {FilePathFilter("build/*")};
	std::set<FilePathFilter> includeFilters = {FilePathFilter("src/*")};
	FilePath workingDir("/home/user/project");
	std::vector<std::string> flags = {"-std=c++17", "-Wall", "-DFOO=1"};

	auto cmd = std::make_shared<IndexerCommand>(
		FilePath("/home/user/project/main.cpp"),
		IndexerCommandCxx(
		FilePath("/home/user/project/main.cpp"),
		indexedPaths,
		excludeFilters,
		includeFilters,
		workingDir,
		flags,
		std::string{}));

	std::vector<std::shared_ptr<IndexerCommand>> commands = {cmd};

	auto buf = IpcSerializer::serializeIndexerCommands(commands);
	auto result = IpcSerializer::deserializeIndexerCommands(buf.data(), buf.size());

	REQUIRE(result.size() == 1);
	const auto cxx = result[0]->target<IndexerCommandCxx>();
	REQUIRE(cxx.has_value());
	REQUIRE(cxx->getSourceFilePath().str() == "/home/user/project/main.cpp");
	REQUIRE(cxx->getIndexedPaths() == indexedPaths);
	REQUIRE(cxx->getWorkingDirectory().str() == "/home/user/project");
	REQUIRE(cxx->getCompilerFlags() == flags);
	REQUIRE(cxx->getExcludeFilters().size() == 1);
	REQUIRE(cxx->getIncludeFilters().size() == 1);
#else
	SUCCEED("CXX language package disabled.");
#endif
}
