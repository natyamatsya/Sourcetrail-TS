// Inline implementations for CdbLoad.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/Tooling/CompilationDatabase.h>
#include <llvm/Support/VirtualFileSystem.h>
#endif

namespace utility
{
inline std::expected<std::shared_ptr<clang::tooling::CompilationDatabase>, CdbLoadError> loadCDB(const FilePath& cdbPath)
{
	if (cdbPath.empty() || !cdbPath.exists())
	{
		return std::unexpected(CdbLoadError{CdbLoadError::Code::PathMissing, {}});
	}

	std::string errorString;
	std::unique_ptr<clang::tooling::CompilationDatabase> cdb = clang::tooling::JSONCompilationDatabase::loadFromFile(
		cdbPath.str(), errorString, clang::tooling::JSONCommandLineSyntax::AutoDetect);
	if (cdb == nullptr)
	{
		return std::unexpected(CdbLoadError{CdbLoadError::Code::ParseFailed, std::move(errorString)});
	}

	return std::shared_ptr<clang::tooling::CompilationDatabase>(
		expandResponseFiles(std::move(cdb), llvm::vfs::getRealFileSystem()));
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
