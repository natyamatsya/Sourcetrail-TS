#ifndef INDEXER_COMMAND_CXX_H
#define INDEXER_COMMAND_CXX_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "FilePath.h"
#include "FilePathFilter.h"
#include "IndexerCommandType.h"

// In the module purview the real clang::tooling declarations come from the wrapper's Clang GMF
// includes; a purview forward declaration would attach a DIFFERENT CompilationDatabase entity to
// the module and clash with them.
namespace clang::tooling
{
class CompilationDatabase;
}
#endif

// Cxx indexer-command payload: a plain value satisfying IndexerCommandC (no base class). The common data
// (source file / source group) lives in the wrapping IndexerCommand, so this holds only Cxx-specific data.
// The static helpers below are Clang-backed utilities that build Cxx commands; they stay in lib_cxx.
SRCTRL_EXPORT class IndexerCommandCxx
{
public:
	static std::vector<FilePath> getSourceFilesFromCDB(const FilePath& cdbPath);
	static std::vector<FilePath> getSourceFilesFromCDB(std::shared_ptr<clang::tooling::CompilationDatabase> cdb, const FilePath& cdbPath);

	static std::string getCompilerFlagLanguageStandard(const std::string& languageStandard);
	static std::vector<std::string> getCompilerFlagsForSystemHeaderSearchPaths(
		const std::vector<FilePath>& systemHeaderSearchPaths);
	static std::vector<std::string> getCompilerFlagsForFrameworkSearchPaths(
		const std::vector<FilePath>& frameworkSearchPaths);

	//! macOS SDK sysroot (via `xcrun --show-sdk-path`), memoized; empty on non-mac
	//! or if detection fails. Sourcetrail's libclang replaces the real compiler, so
	//! the SDK the driver would find natively must be passed explicitly.
	static const std::string& getMacOSSysrootPath();

	//! Returns {"-isysroot", <SDK>} on macOS when `existingFlags` carries no sysroot,
	//! otherwise {}. Injected per translation unit so a CDB-provided sysroot wins.
	static std::vector<std::string> getCompilerFlagsForSysroot(
		const std::vector<std::string>& existingFlags);

	static IndexerCommandType getStaticIndexerCommandType();

	IndexerCommandCxx(
		const FilePath& sourceFilePath,
		const std::set<FilePath>& indexedPaths,
		const std::set<FilePathFilter>& excludeFilters,
		const std::set<FilePathFilter>& includeFilters,
		const FilePath& workingDirectory,
		const std::vector<std::string>& compilerFlags,
		const std::string& compilerPath);

	// IndexerCommandC contract:
	IndexerCommandType getIndexerCommandType() const;
	std::size_t getByteSize(std::size_t stringSize) const;
	std::string getIndexerCommandHash() const;

	// The Cxx parser flow (IndexerCxx/CxxParser) consumes the payload directly, so it keeps its own
	// sourceFilePath (the wrapping IndexerCommand holds the canonical common copy for serialization).
	const FilePath& getSourceFilePath() const;

	//! Stable (FNV-1a) hash of a compile command's flags. Deterministic across
	//! processes so it can be persisted and compared on a later refresh.
	static std::string hashCompilerFlags(const std::vector<std::string>& compilerFlags);

	const std::set<FilePath>& getIndexedPaths() const;
	const std::set<FilePathFilter>& getExcludeFilters() const;
	const std::set<FilePathFilter>& getIncludeFilters() const;
	const std::vector<std::string>& getCompilerFlags() const;
	const FilePath& getWorkingDirectory() const;
	const std::string& getCompilerPath() const;

private:
	FilePath m_sourceFilePath;
	std::set<FilePath> m_indexedPaths;
	std::set<FilePathFilter> m_excludeFilters;
	std::set<FilePathFilter> m_includeFilters;
	FilePath m_workingDirectory;
	std::vector<std::string> m_compilerFlags;
	std::string m_compilerPath;
};

#include "IndexerCommandCxx.inl"

#endif	  // INDEXER_COMMAND_CXX_H
