#ifndef INDEXER_COMMAND_CUSTOM_H
#define INDEXER_COMMAND_CUSTOM_H

#include <cstddef>
#include <string>
#include <vector>

#include "FilePath.h"
#include "IndexerCommandType.h"

// Custom indexer-command payload: a plain value satisfying IndexerCommandC (no base class). Unlike the
// other payloads it keeps its own sourceFilePath, because its %{SOURCE_FILE_PATH} variable substitution
// needs it (the wrapping IndexerCommand still holds the canonical common copy for serialization).
class IndexerCommandCustom
{
public:
	static IndexerCommandType getStaticIndexerCommandType();

	IndexerCommandCustom(
		const std::string& command,
		const std::vector<std::string>& arguments,
		const FilePath& projectFilePath,
		const FilePath& databaseFilePath,
		const std::string& databaseVersion,
		const FilePath& sourceFilePath,
		bool runInParallel);

	IndexerCommandCustom(
		IndexerCommandType type,
		const std::string& command,
		const std::vector<std::string>& arguments,
		const FilePath& projectFilePath,
		const FilePath& databaseFilePath,
		const std::string& databaseVersion,
		const FilePath& sourceFilePath,
		bool runInParallel);

	// IndexerCommandC contract:
	IndexerCommandType getIndexerCommandType() const;
	std::size_t getByteSize(std::size_t stringSize) const;	// Custom historically reported only the base size
	std::string getIndexerCommandHash() const;				// no compile-command hash for Custom

	FilePath getDatabaseFilePath() const;
	void setDatabaseFilePath(const FilePath& databaseFilePath);

	std::string getCommand() const;
	std::vector<std::string> getArguments() const;
	bool getRunInParallel() const;

private:
	std::string replaceVariables(std::string s) const;

	IndexerCommandType m_type;
	std::string m_command;
	std::vector<std::string> m_arguments;
	FilePath m_projectFilePath;
	FilePath m_databaseFilePath;
	std::string m_databaseVersion;
	FilePath m_sourceFilePath;
	bool m_runInParallel;
};

#endif	  // INDEXER_COMMAND_CUSTOM_H
