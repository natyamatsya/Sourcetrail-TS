#include "OptionalRustTestUtils.h"

#include "Catch2.hpp"

#include <set>
#include <vector>

#ifndef SRCTRL_MODULE_BUILD
#include "IndexerCommand.h"
#include "IndexerCommandRust.h"
#include "IndexerCommandSerializer.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.indexer;
import srctrl.interprocess;
#endif

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
	if (!command)
		return {};

	// target<>() already discriminates the payload type, so the monadic chain both
	// checks "is it a Rust command?" and extracts in one expression.
	return command->target<IndexerCommandRust>()
		.transform([](const IndexerCommandRust& rustCommand) {
			return rustCommand.getWorkingDirectory().str();
		})
		.value_or(std::string{});
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
	const auto rustCommand = deserializedCommands.front()->target<IndexerCommandRust>();
	REQUIRE(rustCommand.has_value());
	REQUIRE(deserializedCommands.front()->getSourceFilePath().str() == sourceFilePath.str());
	REQUIRE(rustCommand->getIndexedPaths() == indexedPaths);
	REQUIRE(rustCommand->getWorkingDirectory().str() == workingDirectory.str());
	// Implicit-specialization node scope survives the round-trip (§7).
	REQUIRE(rustCommand->getSpecializationScope() == "all");
	// Package-restriction flag survives the round-trip (crate fan-out R1b).
	REQUIRE(rustCommand->getRestrictToPackage());
}
