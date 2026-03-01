#include "IndexerCommandSwift.h"

#include <QJsonArray>
#include <QJsonObject>

IndexerCommandType IndexerCommandSwift::getStaticIndexerCommandType()
{
	return IndexerCommandType::INDEXER_COMMAND_SWIFT;
}

IndexerCommandSwift::IndexerCommandSwift(
	const FilePath& sourceFilePath,
	const std::set<FilePath>& indexedPaths,
	const FilePath& workingDirectory)
	: IndexerCommand(sourceFilePath)
	, m_indexedPaths(indexedPaths)
	, m_workingDirectory(workingDirectory)
{
}

IndexerCommandType IndexerCommandSwift::getIndexerCommandType() const
{
	return getStaticIndexerCommandType();
}

const std::set<FilePath>& IndexerCommandSwift::getIndexedPaths() const
{
	return m_indexedPaths;
}

const FilePath& IndexerCommandSwift::getWorkingDirectory() const
{
	return m_workingDirectory;
}

QJsonObject IndexerCommandSwift::doSerialize() const
{
	QJsonObject obj = IndexerCommand::doSerialize();

	QJsonArray paths;
	for (const FilePath& p: m_indexedPaths)
		paths.append(QString::fromStdString(p.str()));
	obj["indexed_paths"] = paths;
	obj["working_directory"] = QString::fromStdString(m_workingDirectory.str());

	return obj;
}
