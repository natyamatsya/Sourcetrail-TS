#include "IndexerCommand.h"

IndexerCommand::IndexerCommand(const FilePath& sourceFilePath): m_sourceFilePath(sourceFilePath) {}

size_t IndexerCommand::getByteSize(size_t  /*stringSize*/) const
{
	return m_sourceFilePath.str().size() + m_sourceGroupId.size();
}

const FilePath& IndexerCommand::getSourceFilePath() const
{
	return m_sourceFilePath;
}

const std::string& IndexerCommand::getSourceGroupId() const
{
	return m_sourceGroupId;
}

void IndexerCommand::setSourceGroupId(const std::string& sourceGroupId)
{
	m_sourceGroupId = sourceGroupId;
}
