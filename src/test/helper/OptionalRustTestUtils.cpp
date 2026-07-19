#include "OptionalRustTestUtils.h"

#include "Catch2.hpp"

#include <set>
#include <vector>

#include "IndexerCommand.h"
#include "IndexerCommandRust.h"
#include "IndexerCommandSerializer.h"

std::shared_ptr<IndexerCommand> makeOptionalRustCommand(const std::string& workingDirectory)
{
	const FilePath workingDirectoryPath{workingDirectory};
	return std::make_shared<IndexerCommand>(
		workingDirectoryPath,
		IndexerCommandRust(std::set<FilePath>{workingDirectoryPath}, workingDirectoryPath));
}

bool isOptionalRustCommand(const std::shared_ptr<IndexerCommand>& command)
{
	if (!command)
		return false;

	return command->getIndexerCommandType() == IndexerCommandType::INDEXER_COMMAND_RUST;
}

std::string getOptionalRustWorkingDirectory(const std::shared_ptr<IndexerCommand>& command)
{
	if (!isOptionalRustCommand(command))
		return {};

	const auto* rustCommand = command->target<IndexerCommandRust>();
	if (!rustCommand)
		return {};

	return rustCommand->getWorkingDirectory().str();
}

void assertOptionalRustSerializerRoundTrip()
{
	const std::set<FilePath> indexedPaths{FilePath{"/rust/pkg"}, FilePath{"/rust/pkg/deps"}};
	const FilePath sourceFilePath{"/rust/pkg/src/main.rs"};
	const FilePath workingDirectory{"/rust/pkg"};

	const auto command = std::make_shared<IndexerCommand>(
		sourceFilePath,
		IndexerCommandRust(
		indexedPaths,
		workingDirectory,
		std::vector<std::string>{},
		false,
		false,
		"",
		"all",
		true));
	const std::vector<std::shared_ptr<IndexerCommand>> commands{command};

	const auto buffer = IpcSerializer::serializeIndexerCommands(commands);
	const auto deserializedCommands =
		IpcSerializer::deserializeIndexerCommands(buffer.data(), buffer.size());

	REQUIRE(deserializedCommands.size() == 1);
	const auto* rustCommand = deserializedCommands.front()->target<IndexerCommandRust>();
	REQUIRE(rustCommand != nullptr);
	REQUIRE(deserializedCommands.front()->getSourceFilePath().str() == sourceFilePath.str());
	REQUIRE(rustCommand->getIndexedPaths() == indexedPaths);
	REQUIRE(rustCommand->getWorkingDirectory().str() == workingDirectory.str());
	// Implicit-specialization node scope survives the round-trip (§7).
	REQUIRE(rustCommand->getSpecializationScope() == "all");
	// Package-restriction flag survives the round-trip (crate fan-out R1b).
	REQUIRE(rustCommand->getRestrictToPackage());
}
