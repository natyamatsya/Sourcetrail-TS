#include "OptionalSwiftTestUtils.h"

#include "Catch2.hpp"

#include <set>
#include <vector>

#ifndef SRCTRL_MODULE_BUILD
#include "IndexerCommandSerializer.h"
#include "IndexerCommand.h"
#include "IndexerCommandSwift.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.indexer;
import srctrl.interprocess;
#endif

std::shared_ptr<IndexerCommand> makeOptionalSwiftCommand(const std::string& workingDirectory)
{
	const FilePath workingDirectoryPath{workingDirectory};
	return std::make_shared<IndexerCommand>(
		workingDirectoryPath,
		IndexerCommandSwift(std::set<FilePath>{workingDirectoryPath}, workingDirectoryPath));
}

bool isOptionalSwiftCommand(const std::shared_ptr<IndexerCommand>& command)
{
	if (!command)
		return false;

	return command->getIndexerCommandType() == IndexerCommandType::INDEXER_COMMAND_SWIFT;
}

std::string getOptionalSwiftWorkingDirectory(const std::shared_ptr<IndexerCommand>& command)
{
	if (!command)
		return {};

	// target<>() already discriminates the payload type, so the monadic chain both
	// checks "is it a Swift command?" and extracts in one expression.
	return command->target<IndexerCommandSwift>()
		.transform([](const IndexerCommandSwift& swiftCommand) {
			return swiftCommand.getWorkingDirectory().str();
		})
		.value_or(std::string{});
}

void assertOptionalSwiftSerializerRoundTrip()
{
	const std::set<FilePath> indexedPaths{FilePath{"/swift/pkg"}, FilePath{"/swift/pkg/modules"}};
	const FilePath sourceFilePath{"/swift/pkg/main.swift"};
	const FilePath workingDirectory{"/swift/pkg"};

	const std::vector<std::string> buildArgs{"--configuration", "release"};
	const std::string toolchainPath{"/opt/swift-6.1"};
	const std::string indexStorePath{"/prebuilt/index/store"};
	const std::string specializationScope{"all"};

	const auto command = std::make_shared<IndexerCommand>(
		sourceFilePath,
		IndexerCommandSwift(
			indexedPaths, workingDirectory, buildArgs, toolchainPath, indexStorePath, specializationScope));
	const std::vector<std::shared_ptr<IndexerCommand>> commands{command};

	const auto buffer = IpcSerializer::serializeIndexerCommands(commands);
	const auto deserializedCommands =
		IpcSerializer::deserializeIndexerCommands(buffer.data(), buffer.size());

	REQUIRE(deserializedCommands.size() == 1);
	const auto swiftCommand = deserializedCommands.front()->target<IndexerCommandSwift>();
	REQUIRE(swiftCommand.has_value());
	REQUIRE(deserializedCommands.front()->getSourceFilePath().str() == sourceFilePath.str());
	REQUIRE(swiftCommand->getIndexedPaths() == indexedPaths);
	REQUIRE(swiftCommand->getWorkingDirectory().str() == workingDirectory.str());
	// Swift project-model options (SW5) survive the round-trip.
	REQUIRE(swiftCommand->getBuildArgs() == buildArgs);
	REQUIRE(swiftCommand->getToolchainPath() == toolchainPath);
	REQUIRE(swiftCommand->getIndexStorePath() == indexStorePath);
	REQUIRE(swiftCommand->getSpecializationScope() == specializationScope);
}
