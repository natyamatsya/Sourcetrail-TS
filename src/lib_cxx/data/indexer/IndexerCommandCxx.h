#ifndef INDEXER_COMMAND_CXX_H
#define INDEXER_COMMAND_CXX_H

#include <string>
#include <vector>

#include "IndexerCommand.h"

class FilePath;

namespace clang::tooling
{
class CompilationDatabase;
}

class IndexerCommandCxx: public IndexerCommand
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

	IndexerCommandType getIndexerCommandType() const override;
	size_t getByteSize(size_t stringSize) const override;

	const std::set<FilePath>& getIndexedPaths() const;
	const std::set<FilePathFilter>& getExcludeFilters() const;
	const std::set<FilePathFilter>& getIncludeFilters() const;
	const std::vector<std::string>& getCompilerFlags() const;
	const FilePath& getWorkingDirectory() const;
	const std::string& getCompilerPath() const;

protected:
	QJsonObject doSerialize() const override;

private:
	std::set<FilePath> m_indexedPaths;
	std::set<FilePathFilter> m_excludeFilters;
	std::set<FilePathFilter> m_includeFilters;
	FilePath m_workingDirectory;
	std::vector<std::string> m_compilerFlags;
	std::string m_compilerPath;
};

#endif	  // INDEXER_COMMAND_CXXL_H
