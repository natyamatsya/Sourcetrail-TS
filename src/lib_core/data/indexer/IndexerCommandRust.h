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
		const std::string& targetTriple = "",
		const std::string& specializationScope = "local",
		bool restrictToPackage = false);

	IndexerCommandType getIndexerCommandType() const override;

	const std::set<FilePath>& getIndexedPaths() const;
	const FilePath& getWorkingDirectory() const;

	// Cargo project-model options (project model v1: feature selection and
	// target triple; see context/DESIGN_RUST_PROJECT_MODEL.md)
	const std::vector<std::string>& getFeatures() const;
	bool getAllFeatures() const;
	bool getNoDefaultFeatures() const;
	const std::string& getTargetTriple() const;

	// Implicit generic-specialization node scope ("off"/"local"/"all"; §7 of
	// context/DESIGN_RUST_TYPE_SYSTEM_EDGES.md). Empty maps to the default.
	const std::string& getSpecializationScope() const;

	// Crate fan-out R1b: true = the subprocess collects only the package
	// rooted at the working directory (per-member commands); false = it
	// collects the whole loaded workspace (legacy / fallback commands).
	bool getRestrictToPackage() const;

protected:

private:
	std::set<FilePath> m_indexedPaths;
	FilePath m_workingDirectory;
	std::vector<std::string> m_features;
	bool m_allFeatures;
	bool m_noDefaultFeatures;
	std::string m_targetTriple;
	std::string m_specializationScope;
	bool m_restrictToPackage;
};

#endif	  // INDEXER_COMMAND_RUST_H
