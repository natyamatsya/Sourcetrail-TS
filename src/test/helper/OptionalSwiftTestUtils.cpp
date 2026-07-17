#include "OptionalSwiftTestUtils.h"

#include "Catch2.hpp"

#include <set>
#include <vector>

#include "IndexerCommandSerializer.h"
#include "IndexerCommandSwift.h"

std::shared_ptr<IndexerCommand> makeOptionalSwiftCommand(const std::string& workingDirectory)
{
	const FilePath workingDirectoryPath{workingDirectory};
	return std::make_shared<IndexerCommandSwift>(
		workingDirectoryPath,
		std::set<FilePath>{workingDirectoryPath},
		workingDirectoryPath);
}

bool isOptionalSwiftCommand(const std::shared_ptr<IndexerCommand>& command)
{
	if (!command)
		return false;

	return command->getIndexerCommandType() == IndexerCommandType::INDEXER_COMMAND_SWIFT;
}

std::string getOptionalSwiftWorkingDirectory(const std::shared_ptr<IndexerCommand>& command)
{
	if (!isOptionalSwiftCommand(command))
		return {};

	const auto* swiftCommand = dynamic_cast<const IndexerCommandSwift*>(command.get());
	if (!swiftCommand)
		return {};

	return swiftCommand->getWorkingDirectory().str();
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

	const auto command = std::make_shared<IndexerCommandSwift>(
		sourceFilePath,
		indexedPaths,
		workingDirectory,
		buildArgs,
		toolchainPath,
		indexStorePath,
		specializationScope);
	const std::vector<std::shared_ptr<IndexerCommand>> commands{command};

	const auto buffer = IpcSerializer::serializeIndexerCommands(commands);
	const auto deserializedCommands =
		IpcSerializer::deserializeIndexerCommands(buffer.data(), buffer.size());

	REQUIRE(deserializedCommands.size() == 1);
	const auto* swiftCommand = dynamic_cast<const IndexerCommandSwift*>(deserializedCommands.front().get());
	REQUIRE(swiftCommand != nullptr);
	REQUIRE(swiftCommand->getSourceFilePath().str() == sourceFilePath.str());
	REQUIRE(swiftCommand->getIndexedPaths() == indexedPaths);
	REQUIRE(swiftCommand->getWorkingDirectory().str() == workingDirectory.str());
	// Swift project-model options (SW5) survive the round-trip.
	REQUIRE(swiftCommand->getBuildArgs() == buildArgs);
	REQUIRE(swiftCommand->getToolchainPath() == toolchainPath);
	REQUIRE(swiftCommand->getIndexStorePath() == indexStorePath);
	REQUIRE(swiftCommand->getSpecializationScope() == specializationScope);
}
