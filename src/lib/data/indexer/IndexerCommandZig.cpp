#include "IndexerCommandZig.h"

#include <QJsonArray>
#include <QJsonObject>

IndexerCommandType IndexerCommandZig::getStaticIndexerCommandType()
{
	return IndexerCommandType::INDEXER_COMMAND_ZIG;
}

IndexerCommandZig::IndexerCommandZig(
	const FilePath& sourceFilePath,
	const std::set<FilePath>& indexedPaths,
	const FilePath& workingDirectory)
	: IndexerCommand(sourceFilePath)
	, m_indexedPaths(indexedPaths)
	, m_workingDirectory(workingDirectory)
{
}

IndexerCommandType IndexerCommandZig::getIndexerCommandType() const
{
	return getStaticIndexerCommandType();
}

const std::set<FilePath>& IndexerCommandZig::getIndexedPaths() const
{
	return m_indexedPaths;
}

const FilePath& IndexerCommandZig::getWorkingDirectory() const
{
	return m_workingDirectory;
}

QJsonObject IndexerCommandZig::doSerialize() const
{
	QJsonObject obj = IndexerCommand::doSerialize();

	QJsonArray paths;
	for (const FilePath& p : m_indexedPaths)
		paths.append(QString::fromStdString(p.str()));
	obj["indexed_paths"] = paths;
	obj["working_directory"] = QString::fromStdString(m_workingDirectory.str());

	return obj;
}
