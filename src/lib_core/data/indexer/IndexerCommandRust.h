#ifndef INDEXER_COMMAND_RUST_H
#define INDEXER_COMMAND_RUST_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "FilePath.h"
#include "IndexerCommandType.h"
#endif

// Rust indexer-command payload: a plain value satisfying IndexerCommandC (no base class). The common data
// (source file / source group) lives in the wrapping IndexerCommand, so this holds only Rust-specific data.
SRCTRL_EXPORT class IndexerCommandRust
{
public:
	static IndexerCommandType getStaticIndexerCommandType();

	IndexerCommandRust(
		const std::set<FilePath>& indexedPaths,
		const FilePath& workingDirectory,
		const std::vector<std::string>& features = {},
		bool allFeatures = false,
		bool noDefaultFeatures = false,
		const std::string& targetTriple = "",
		const std::string& specializationScope = "local",
		bool restrictToPackage = false);

	// IndexerCommandC contract:
	IndexerCommandType getIndexerCommandType() const;
	std::size_t getByteSize(std::size_t stringSize) const;	// Rust historically reported only the base size
	std::string getIndexerCommandHash() const;				// no compile-command hash for Rust

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

#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerCommandRust.inl"
#endif

#endif	  // INDEXER_COMMAND_RUST_H
