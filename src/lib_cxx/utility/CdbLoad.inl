// Inline implementations for CdbLoad.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <filesystem>
#include <format>
#include <mutex>
#include <system_error>

#include <clang/Tooling/CompilationDatabase.h>

#include "FileSystem.h"
#include <llvm/Support/VirtualFileSystem.h>
#endif

namespace utility
{
inline std::string describe(const CdbLoadError& error)
{
	return error.message.empty() ? std::string(to_std_sv(error.code)) : error.message;
}

inline std::expected<std::shared_ptr<clang::tooling::CompilationDatabase>, CdbLoadError> loadCDB(const FilePath& cdbPath)
{
	using enum CdbLoadError::Code;
	if (cdbPath.empty())
	{
		return std::unexpected(CdbLoadError{PathMissing, "No compilation database is set."});
	}
	if (!cdbPath.exists())
	{
		return std::unexpected(CdbLoadError{
			PathMissing, std::format("The compilation database \"{}\" does not exist.", cdbPath.str())});
	}
	if (cdbPath.isDirectory())
	{
		return std::unexpected(CdbLoadError{
			NotAFile,
			std::format(
				"The compilation database \"{}\" is a directory, not a compile_commands.json file.",
				cdbPath.str())});
	}

	if (FileSystem::getFileByteSize(cdbPath) == 0)
	{
		return std::unexpected(CdbLoadError{
			ParseFailed, std::format("The compilation database \"{}\" is empty.", cdbPath.str())});
	}

	// One refresh loads the database several times (the check, the files to index, the commands, the
	// command hashes), and parsing a large one takes seconds each time: the last database loaded is
	// kept while its file is unchanged (same size and modification time).
	struct Loaded
	{
		std::string path;
		std::uintmax_t size = 0;
		std::filesystem::file_time_type modified;
		std::shared_ptr<clang::tooling::CompilationDatabase> cdb;
	};
	static std::mutex loadedMutex;
	static Loaded loaded;
	std::error_code ec;
	const std::uintmax_t size = std::filesystem::file_size(cdbPath.getPath(), ec);
	const std::filesystem::file_time_type modified =
		ec ? std::filesystem::file_time_type() : std::filesystem::last_write_time(cdbPath.getPath(), ec);
	if (!ec)
	{
		const std::lock_guard<std::mutex> lock(loadedMutex);
		if (loaded.cdb != nullptr && loaded.path == cdbPath.str() && loaded.size == size &&
			loaded.modified == modified)
		{
			return loaded.cdb;
		}
	}

	std::string errorString;
	std::unique_ptr<clang::tooling::CompilationDatabase> cdb = clang::tooling::JSONCompilationDatabase::loadFromFile(
		cdbPath.str(), errorString, clang::tooling::JSONCommandLineSyntax::AutoDetect);
	if (cdb == nullptr)
	{
		return std::unexpected(CdbLoadError{
			ParseFailed,
			std::format("The compilation database \"{}\" cannot be read: {}", cdbPath.str(), errorString)});
	}
	// An empty array parses; it compiles nothing, which is never what a person meant to index.
	if (cdb->getAllFiles().empty())
	{
		return std::unexpected(CdbLoadError{
			NoCompileCommands,
			std::format("The compilation database \"{}\" has no compile commands.", cdbPath.str())});
	}

	std::shared_ptr<clang::tooling::CompilationDatabase> expanded(
		expandResponseFiles(std::move(cdb), llvm::vfs::getRealFileSystem()));
	if (!ec)
	{
		const std::lock_guard<std::mutex> lock(loadedMutex);
		loaded = Loaded{cdbPath.str(), size, modified, expanded};
	}
	return expanded;
}

inline std::expected<std::shared_ptr<clang::tooling::CompilationDatabase>, CdbLoadError> loadCDB(
	std::string_view cdbContent, clang::tooling::JSONCommandLineSyntax syntax)
{
	if (cdbContent.empty())
	{
		return std::unexpected(CdbLoadError{CdbLoadError::Code::PathMissing, {}});
	}

	std::string errorString;
	std::unique_ptr<clang::tooling::CompilationDatabase> cdb =
		clang::tooling::JSONCompilationDatabase::loadFromBuffer(cdbContent, errorString, syntax);
	if (cdb == nullptr)
	{
		return std::unexpected(CdbLoadError{CdbLoadError::Code::ParseFailed, std::move(errorString)});
	}

	return std::shared_ptr<clang::tooling::CompilationDatabase>(std::move(cdb));
}
}	 // namespace utility
