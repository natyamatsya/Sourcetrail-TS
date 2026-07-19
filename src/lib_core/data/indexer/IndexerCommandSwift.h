#ifndef INDEXER_COMMAND_SWIFT_H
#define INDEXER_COMMAND_SWIFT_H

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "FilePath.h"
#include "IndexerCommandType.h"

// Swift indexer-command payload: a plain value satisfying IndexerCommandC (no base class). The common data
// (source file / source group) lives in the wrapping IndexerCommand, so this holds only Swift-specific data.
class IndexerCommandSwift
{
public:
	static IndexerCommandType getStaticIndexerCommandType();

	IndexerCommandSwift(
		const std::set<FilePath>& indexedPaths,
		const FilePath& workingDirectory,
		const std::vector<std::string>& buildArgs = {},
		const std::string& toolchainPath = "",
		const std::string& indexStorePath = "",
		const std::string& specializationScope = "");

	// IndexerCommandC contract:
	IndexerCommandType getIndexerCommandType() const;
	std::size_t getByteSize(std::size_t stringSize) const;	// Swift historically reported only the base size
	std::string getIndexerCommandHash() const;				// no compile-command hash for Swift

	const std::set<FilePath>& getIndexedPaths() const;
	const FilePath& getWorkingDirectory() const;

	// Swift project-model options (SW5). See indexer_command.fbs.
	const std::vector<std::string>& getBuildArgs() const;
	const std::string& getToolchainPath() const;
	const std::string& getIndexStorePath() const;
	// Type-argument edge scope for `Base<Arg>` use sites (SW11): "off"/"local"/"all".
	const std::string& getSpecializationScope() const;

private:
	std::set<FilePath> m_indexedPaths;
	FilePath m_workingDirectory;
	std::vector<std::string> m_buildArgs;
	std::string m_toolchainPath;
	std::string m_indexStorePath;
	std::string m_specializationScope;
};

#endif	  // INDEXER_COMMAND_SWIFT_H
