#ifndef UTILITY_SOURCE_GROUP_CXX_H
#define UTILITY_SOURCE_GROUP_CXX_H

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <clang/Tooling/JSONCompilationDatabase.h>

class DialogView;
class FilePath;
class SourceGroupSettingsWithCxxPchOptions;
class StorageProvider;
class Task;

namespace utility
{
//! Builds a Sourcetrail precompiled header for `pchInputFilePath` at
//! `pchOutputFilePath` using the given flags + compiler (so the resource dir
//! matches the translation units). Consumed via getIncludePchFlagsForOutput.
//!
//! The PCH is (re)built on every index, never reused across runs: building also
//! indexes the header's symbols into `storageProvider` exactly once (translation
//! units skip re-recording PCH-loaded declarations), so a reused PCH would leave
//! those symbols missing whenever the database was rebuilt from empty.
std::shared_ptr<Task> createBuildPchTaskForInput(
	const FilePath& pchInputFilePath,
	const FilePath& pchOutputFilePath,
	std::vector<std::string> compilerFlags,
	const std::string& compilerPath,
	std::shared_ptr<StorageProvider> storageProvider,
	std::shared_ptr<DialogView> dialogView);

std::shared_ptr<Task> createBuildPchTask(
	const SourceGroupSettingsWithCxxPchOptions* settings,
	std::vector<std::string> compilerFlags,
	std::shared_ptr<StorageProvider> storageProvider,
	std::shared_ptr<DialogView> dialogView);

//! Why a compilation database failed to load. Carries the clang JSON diagnostic in `message` for
//! ParseFailed (surfaced to the user in the project wizard); to_std_sv() gives a generic fallback.
struct CdbLoadError
{
	enum class Code : std::uint8_t
	{
		PathMissing,	// the .srctrl.toml path is empty or the file does not exist
		ParseFailed,	// clang could not parse the compilation database
	};
	Code code{};
	std::string message;
};

constexpr std::string_view to_std_sv(CdbLoadError::Code code) noexcept
{
	using enum CdbLoadError::Code;
	switch (code)
	{
	case PathMissing:
		return "compilation database path is empty or does not exist";
	case ParseFailed:
		return "compilation database could not be parsed";
	}
	return "unknown error";
}

std::expected<std::shared_ptr<clang::tooling::CompilationDatabase>, CdbLoadError> loadCDB(
	const FilePath& cdbPath);

std::expected<std::shared_ptr<clang::tooling::CompilationDatabase>, CdbLoadError> loadCDB(
	std::string_view cdbContent, clang::tooling::JSONCommandLineSyntax syntax);

bool containsIncludePchFlags(std::shared_ptr<clang::tooling::CompilationDatabase> cdb);
bool containsIncludePchFlag(const std::vector<std::string>& args);
std::vector<std::string> getWithRemoveIncludePchFlag(const std::vector<std::string>& args);
void removeIncludePchFlag(std::vector<std::string>& args);
std::vector<std::string> getIncludePchFlags(const SourceGroupSettingsWithCxxPchOptions* settings);
std::vector<std::string> getIncludePchFlagsForOutput(const FilePath& pchOutputFilePath);

//! Failure modes of runIndexerPrebuildMode().
enum class IndexerPrebuildError : std::uint8_t
{
	IndexerExecutableMissing,
	SubprocessFailed,
};

constexpr std::string_view to_std_sv(IndexerPrebuildError error) noexcept
{
	using enum IndexerPrebuildError;
	switch (error)
	{
	case IndexerExecutableMissing:
		return "sourcetrail_indexer executable is missing";
	case SubprocessFailed:
		return "indexer prebuild subprocess exited non-zero";
	}
	return "unknown error";
}

//! Spawns the sourcetrail_indexer executable in a pre-index mode (e.g.
//! "--prebuild-modules=<request>" or "--prebuild-pch=<request>") and blocks until it exits, so the
//! parse-arbitrary-user-code work is crash-isolated from the main app process.
std::expected<void, IndexerPrebuildError> runIndexerPrebuildMode(const std::string& modeArgument);
}

#endif
