#ifndef CDB_LOAD_H
#define CDB_LOAD_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include <clang/Tooling/JSONCompilationDatabase.h>

#include "FilePath.h"
#endif

SRCTRL_EXPORT namespace utility
{
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
}

#include "CdbLoad.inl"

#endif	  // CDB_LOAD_H
