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
	const FilePath& workingDirectory,
	const std::vector<std::string>& buildArgs,
	const std::string& toolchainPath,
	const std::string& indexStorePath,
	const std::string& specializationScope)
	: IndexerCommand(sourceFilePath)
	, m_indexedPaths(indexedPaths)
	, m_workingDirectory(workingDirectory)
	, m_buildArgs(buildArgs)
	, m_toolchainPath(toolchainPath)
	, m_indexStorePath(indexStorePath)
	, m_specializationScope(specializationScope)
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

const std::vector<std::string>& IndexerCommandSwift::getBuildArgs() const
{
	return m_buildArgs;
}

const std::string& IndexerCommandSwift::getToolchainPath() const
{
	return m_toolchainPath;
}

const std::string& IndexerCommandSwift::getIndexStorePath() const
{
	return m_indexStorePath;
}

const std::string& IndexerCommandSwift::getSpecializationScope() const
{
	return m_specializationScope;
}

QJsonObject IndexerCommandSwift::doSerialize() const
{
	QJsonObject obj = IndexerCommand::doSerialize();

	QJsonArray paths;
	for (const FilePath& p: m_indexedPaths)
		paths.append(QString::fromStdString(p.str()));
	obj["indexed_paths"] = paths;
	obj["working_directory"] = QString::fromStdString(m_workingDirectory.str());

	QJsonArray buildArgs;
	for (const std::string& a: m_buildArgs)
		buildArgs.append(QString::fromStdString(a));
	obj["swift_build_args"] = buildArgs;
	obj["swift_toolchain_path"] = QString::fromStdString(m_toolchainPath);
	obj["swift_index_store_path"] = QString::fromStdString(m_indexStorePath);
	obj["swift_specialization_scope"] = QString::fromStdString(m_specializationScope);

	return obj;
}
