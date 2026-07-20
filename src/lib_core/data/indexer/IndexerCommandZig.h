#ifndef INDEXER_COMMAND_ZIG_H
#define INDEXER_COMMAND_ZIG_H

#include <cstddef>
#include <set>
#include <string>

#include "FilePath.h"
#include "IndexerCommandType.h"

// Zig indexer-command payload: a plain value satisfying IndexerCommandC (no base class). The common data
// (source file / source group) lives in the wrapping IndexerCommand, so this holds only Zig-specific data.
class IndexerCommandZig
{
public:
	static IndexerCommandType getStaticIndexerCommandType();

	IndexerCommandZig(const std::set<FilePath>& indexedPaths, const FilePath& workingDirectory);

	// IndexerCommandC contract:
	IndexerCommandType getIndexerCommandType() const;
	std::size_t getByteSize(std::size_t stringSize) const;	// Zig reports only the base size
	std::string getIndexerCommandHash() const;				// no compile-command hash for Zig

	const std::set<FilePath>& getIndexedPaths() const;
	const FilePath& getWorkingDirectory() const;

private:
	std::set<FilePath> m_indexedPaths;
	FilePath m_workingDirectory;
};

#endif	  // INDEXER_COMMAND_ZIG_H
