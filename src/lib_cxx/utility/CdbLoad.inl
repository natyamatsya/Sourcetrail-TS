// Inline implementations for CdbLoad.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <system_error>

#include <clang/Tooling/CompilationDatabase.h>

#include "FileSystem.h"
#include <llvm/ADT/ArrayRef.h>	// the ArrayRef xxh3_64bits, in LLVM 22 and 23
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/xxhash.h>
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
	// kept while its content is unchanged. Its size and modification time could not tell: a rewrite
	// of the same size within one timestamp tick (a flag swapped for another of its length) kept the
	// old commands. Hashing the file costs a fraction of parsing it.
	struct Loaded
	{
		std::string path;
		std::uint64_t contentHash = 0;
		std::shared_ptr<clang::tooling::CompilationDatabase> cdb;
	};
	static std::mutex loadedMutex;
	static Loaded loaded;

	std::string content;
	{
		std::ifstream file(cdbPath.getPath(), std::ios::binary | std::ios::ate);
		const std::streamoff length = file ? static_cast<std::streamoff>(file.tellg()) : -1;
		if (length < 0)
		{
			return std::unexpected(CdbLoadError{
				ParseFailed, std::format("The compilation database \"{}\" cannot be opened.", cdbPath.str())});
		}
		content.resize(static_cast<std::size_t>(length));
		file.seekg(0);
		if (!file.read(content.data(), length))
		{
			return std::unexpected(CdbLoadError{
				ParseFailed, std::format("The compilation database \"{}\" cannot be read.", cdbPath.str())});
		}
	}
	const std::uint64_t contentHash =
		llvm::xxh3_64bits(llvm::ArrayRef(reinterpret_cast<const std::uint8_t*>(content.data()), content.size()));
	{
		const std::lock_guard<std::mutex> lock(loadedMutex);
		if (loaded.cdb != nullptr && loaded.path == cdbPath.str() && loaded.contentHash == contentHash)
		{
			return loaded.cdb;
		}
	}

	std::string errorString;
	std::unique_ptr<clang::tooling::CompilationDatabase> cdb = clang::tooling::JSONCompilationDatabase::loadFromBuffer(
		content, errorString, clang::tooling::JSONCommandLineSyntax::AutoDetect);
	content = std::string();	// the database keeps its own copy
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
	{
		const std::lock_guard<std::mutex> lock(loadedMutex);
		loaded = Loaded{cdbPath.str(), contentHash, expanded};
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
