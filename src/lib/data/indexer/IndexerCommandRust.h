#ifndef INDEXER_COMMAND_RUST_H
#define INDEXER_COMMAND_RUST_H

#include <set>
#include <string>
#include <vector>

#include "IndexerCommand.h"

class IndexerCommandRust: public IndexerCommand
{
public:
	static IndexerCommandType getStaticIndexerCommandType();

	IndexerCommandRust(
		const FilePath& sourceFilePath,
		const std::set<FilePath>& indexedPaths,
		const FilePath& workingDirectory,
		const std::vector<std::string>& features = {},
		bool allFeatures = false,
		bool noDefaultFeatures = false,
		const std::string& targetTriple = "");

	IndexerCommandType getIndexerCommandType() const override;

	const std::set<FilePath>& getIndexedPaths() const;
	const FilePath& getWorkingDirectory() const;

	// Cargo project-model options (project model v1: feature selection and
	// target triple; see context/DESIGN_RUST_PROJECT_MODEL.md)
	const std::vector<std::string>& getFeatures() const;
	bool getAllFeatures() const;
	bool getNoDefaultFeatures() const;
	const std::string& getTargetTriple() const;

protected:
	QJsonObject doSerialize() const override;

private:
	std::set<FilePath> m_indexedPaths;
	FilePath m_workingDirectory;
	std::vector<std::string> m_features;
	bool m_allFeatures;
	bool m_noDefaultFeatures;
	std::string m_targetTriple;
};

#endif	  // INDEXER_COMMAND_RUST_H
