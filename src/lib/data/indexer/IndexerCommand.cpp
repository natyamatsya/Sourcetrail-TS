#include "IndexerCommand.h"

#include <QJsonDocument>
#include <QJsonObject>


std::string IndexerCommand::serialize(std::shared_ptr<const IndexerCommand> indexerCommand, bool compact)
{
	QJsonDocument jsonDocument(indexerCommand->doSerialize());
	return QString::fromUtf8(
			   jsonDocument.toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented))
		.toStdString();
}

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

QJsonObject IndexerCommand::doSerialize() const
{
	QJsonObject jsonObject;

	{
		jsonObject["type"] = QString::fromStdString(
			indexerCommandTypeToString(getIndexerCommandType()));
	}
	{
		jsonObject["source_file_path"] = QString::fromStdString(m_sourceFilePath.str());
	}
	if (!m_sourceGroupId.empty())
	{
		jsonObject["source_group_id"] = QString::fromStdString(m_sourceGroupId);
	}

	return jsonObject;
}
