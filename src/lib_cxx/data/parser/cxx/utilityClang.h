#ifndef UTILITY_CLANG_H
#define UTILITY_CLANG_H

#include <clang/AST/Decl.h>
#include <filesystem>
#include <optional>

#include "AccessKind.h"
#include "SymbolKind.h"
#include "DefinitionKind.h"

struct ParseLocation;
struct ParseLocation;
class CanonicalFilePathCache;
class FilePath;

namespace clang
{
class SourceRange;
class Preprocessor;
class SourceManager;
struct PrintingPolicy;
}	 // namespace clang

namespace utility
{
clang::PrintingPolicy makePrintingPolicyForCPlusPlus();

template <typename T>
const T* getFirstDecl(const T* decl);
bool isImplicit(const clang::Decl* d);
DefinitionKind getDefinitionKind(const clang::Decl *d);
AccessKind convertAccessSpecifier(clang::AccessSpecifier access);
SymbolKind convertTagKind(const clang::TagTypeKind tagKind);
bool isLocalVariable(const clang::ValueDecl* d);
bool isParameter(const clang::ValueDecl* d);
SymbolKind getSymbolKind(const clang::VarDecl* d);
std::string getFileNameOfFileEntry(const clang::FileEntryRef &entry);

ParseLocation getParseLocation(
	const clang::SourceLocation& sourceLocation,
	const clang::SourceManager& sourceManager,
	clang::Preprocessor* preprocessor,
	CanonicalFilePathCache& canonicalFilePathCache);

ParseLocation getParseLocation(
	const clang::SourceRange& sourceRange,
	const clang::SourceManager& sourceManager,
	clang::Preprocessor* preprocessor,
	CanonicalFilePathCache& canonicalFilePathCache);

// Returns the compiler's resource directory if it exists on disk, or std::nullopt
// if it doesn't (e.g. /usr/bin/c++ shim on macOS where the real resources live
// inside the Xcode toolchain at a path GetResourcesPath cannot deduce).
std::optional<std::filesystem::path> resolveCompilerResourceDir(const std::filesystem::path& compilerPath);
}	 // namespace utility

template <typename T>
const T* utility::getFirstDecl(const T* decl)
{
	const clang::Decl* ret = decl;
	{
		const clang::Decl* prev = ret;
		while (prev)
		{
			ret = prev;
			prev = prev->getPreviousDecl();
		}
	}
	return clang::dyn_cast_or_null<T>(ret);
}

#endif	  // UTILITY_CLANG_H
