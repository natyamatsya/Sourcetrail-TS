#ifndef OPTIONAL_CXX_TEST_UTILS_H
#define OPTIONAL_CXX_TEST_UTILS_H

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "IndexerCommand.h"

std::shared_ptr<IndexerCommand> makeOptionalCxxCommand(
	const std::string& sourceFilePath,
	const std::set<FilePath>& indexedPaths,
	const std::string& workingDirectory,
	const std::vector<std::string>& compilerFlags);

bool isOptionalCxxCommand(const std::shared_ptr<IndexerCommand>& command);

void assertOptionalCxxSerializerRoundTrip();

#endif	// OPTIONAL_CXX_TEST_UTILS_H
