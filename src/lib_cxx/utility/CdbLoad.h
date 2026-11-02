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
//! Why a compilation database failed to load, or holds nothing to index. `message` names the
//! database and the problem for a person (the wizard, the indexing notice, the CLI); to_std_sv()
//! gives a generic fallback.
struct CdbLoadError
{
	enum class Code : std::uint8_t
	{
		PathMissing,		// the .srctrl.toml path is empty or the file does not exist
		NotAFile,			// the path is a directory (or another non-file)
		ParseFailed,		// clang could not parse the compilation database
		NoCompileCommands,	// it parses, but lists no compile command
		NoSourceFiles,		// none of the files its commands compile exists
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
	case NotAFile:
		return "compilation database path is not a file";
	case ParseFailed:
		return "compilation database could not be parsed";
	case NoCompileCommands:
		return "compilation database has no compile commands";
	case NoSourceFiles:
		return "none of the compilation database's source files exists";
	}
	return "unknown error";
}

//! The message of `error`, or its code's fallback when it has none.
std::string describe(const CdbLoadError& error);

//! Loads the compilation database at `cdbPath`. Fails, naming the problem, when the path is empty,
//! missing or not a file, when clang cannot parse it (invalid JSON, an entry that is not an object, a
//! missing or mistyped "file"/"directory"/"arguments"), and when it lists no compile command.
std::expected<std::shared_ptr<clang::tooling::CompilationDatabase>, CdbLoadError> loadCDB(
	const FilePath& cdbPath);

std::expected<std::shared_ptr<clang::tooling::CompilationDatabase>, CdbLoadError> loadCDB(
	std::string_view cdbContent, clang::tooling::JSONCommandLineSyntax syntax);
}

#include "CdbLoad.inl"

#endif	  // CDB_LOAD_H
