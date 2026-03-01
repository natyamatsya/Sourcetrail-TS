#ifndef OPTIONAL_RUST_TEST_UTILS_H
#define OPTIONAL_RUST_TEST_UTILS_H

#include <memory>
#include <string>

#include "IndexerCommand.h"

std::shared_ptr<IndexerCommand> makeOptionalRustCommand(const std::string& workingDirectory);
bool isOptionalRustCommand(const std::shared_ptr<IndexerCommand>& command);
std::string getOptionalRustWorkingDirectory(const std::shared_ptr<IndexerCommand>& command);

void assertOptionalRustSerializerRoundTrip();

#endif	// OPTIONAL_RUST_TEST_UTILS_H
