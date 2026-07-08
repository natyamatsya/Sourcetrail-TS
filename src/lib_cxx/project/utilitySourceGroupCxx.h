#ifndef UTILITY_SOURCE_GROUP_CXX_H
#define UTILITY_SOURCE_GROUP_CXX_H

#include <memory>
#include <string>
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

std::shared_ptr<clang::tooling::CompilationDatabase> loadCDB(const FilePath& cdbPath, std::string* error = nullptr);
	
std::shared_ptr<clang::tooling::CompilationDatabase> loadCDB(std::string_view cdbContent, clang::tooling::JSONCommandLineSyntax syntax,
	std::string *error);

bool containsIncludePchFlags(std::shared_ptr<clang::tooling::CompilationDatabase> cdb);
bool containsIncludePchFlag(const std::vector<std::string>& args);
std::vector<std::string> getWithRemoveIncludePchFlag(const std::vector<std::string>& args);
void removeIncludePchFlag(std::vector<std::string>& args);
std::vector<std::string> getIncludePchFlags(const SourceGroupSettingsWithCxxPchOptions* settings);
std::vector<std::string> getIncludePchFlagsForOutput(const FilePath& pchOutputFilePath);
}

#endif
