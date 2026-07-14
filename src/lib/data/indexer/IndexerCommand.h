#ifndef INDEXER_COMMAND_H
#define INDEXER_COMMAND_H

#include <set>
#include <string>

#include "FilePath.h"
#include "FilePathFilter.h"
#include "IndexerCommandType.h"

class QJsonObject;

class IndexerCommand
{
public:
	static std::string serialize(
		std::shared_ptr<const IndexerCommand> indexerCommand, bool compact = true);

	IndexerCommand(const FilePath& sourceFilePath);
	virtual ~IndexerCommand() = default;

	virtual IndexerCommandType getIndexerCommandType() const = 0;

	virtual size_t getByteSize(size_t stringSize) const;

	const FilePath& getSourceFilePath() const;

	//! Id of the source group this command belongs to (fan-out S1). Tagged by
	//! CombinedIndexerCommandProvider when commands are consumed for indexing;
	//! empty until then. Lets subprocesses filter the shared command queue by
	//! group (fan-out S2).
	const std::string& getSourceGroupId() const;
	void setSourceGroupId(const std::string& sourceGroupId);

	//! Stable hash of the effective compile command for this translation unit.
	//! Empty for command types with no meaningful compile command. Used by the
	//! incremental refresh to detect flag changes that leave the source mtime
	//! untouched (e.g. an edited define/include in CMakeLists or the CDB).
	virtual std::string getIndexerCommandHash() const { return std::string(); }

protected:
	virtual QJsonObject doSerialize() const;

private:
	FilePath m_sourceFilePath;
	std::string m_sourceGroupId;
};

#endif	  // INDEXER_COMMAND_H
