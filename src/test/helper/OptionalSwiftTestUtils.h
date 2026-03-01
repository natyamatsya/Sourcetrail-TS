#ifndef OPTIONAL_SWIFT_TEST_UTILS_H
#define OPTIONAL_SWIFT_TEST_UTILS_H

#include <memory>
#include <string>

#include "IndexerCommand.h"

std::shared_ptr<IndexerCommand> makeOptionalSwiftCommand(const std::string& workingDirectory);
bool isOptionalSwiftCommand(const std::shared_ptr<IndexerCommand>& command);
std::string getOptionalSwiftWorkingDirectory(const std::shared_ptr<IndexerCommand>& command);

void assertOptionalSwiftSerializerRoundTrip();

#endif	// OPTIONAL_SWIFT_TEST_UTILS_H
