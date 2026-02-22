#include "IndexerCommandRust.h"

#if BUILD_RUST_LANGUAGE_PACKAGE

#include <QJsonArray>
#include <QJsonObject>

IndexerCommandType IndexerCommandRust::getStaticIndexerCommandType()
{
	return INDEXER_COMMAND_RUST;
}

IndexerCommandRust::IndexerCommandRust(
	const FilePath& sourceFilePath,
	const std::set<FilePath>& indexedPaths,
	const FilePath& workingDirectory)
	: IndexerCommand(sourceFilePath)
	, m_indexedPaths(indexedPaths)
	, m_workingDirectory(workingDirectory)
{
}

IndexerCommandType IndexerCommandRust::getIndexerCommandType() const
{
	return getStaticIndexerCommandType();
}

const std::set<FilePath>& IndexerCommandRust::getIndexedPaths() const
{
	return m_indexedPaths;
}

const FilePath& IndexerCommandRust::getWorkingDirectory() const
{
	return m_workingDirectory;
}

QJsonObject IndexerCommandRust::doSerialize() const
{
	QJsonObject obj = IndexerCommand::doSerialize();

	QJsonArray paths;
	for (const FilePath& p : m_indexedPaths)
		paths.append(QString::fromStdString(p.str()));
	obj["indexed_paths"] = paths;
	obj["working_directory"] = QString::fromStdString(m_workingDirectory.str());

	return obj;
}

#endif	  // BUILD_RUST_LANGUAGE_PACKAGE
