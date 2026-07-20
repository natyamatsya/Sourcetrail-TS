// Inline implementations for utilityClang.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Basic/FileManager.h>
#include <clang/Lex/Preprocessor.h>

#include "CanonicalFilePathCache.h"
#include "ParseLocation.h"
#include "ToolChain.h"
#include "clang_compat/ClangCompat.h"
#include "utilityApp.h"
#include "utilityString.h"

#include <filesystem>
#include <map>
#include <mutex>
#endif


inline bool utility::isImplicit(const clang::Decl* d)
{
	if (!d)
	{
		return false;
	}

	if (d->isImplicit())
	{
		if (const clang::RecordDecl* rd = clang::dyn_cast_or_null<clang::RecordDecl>(d))
		{
			if (rd->isLambda())
			{
				return isImplicit(clang::dyn_cast_or_null<clang::Decl>(d->getDeclContext()));
			}
		}
		return true;
	}
	else if (
		const clang::ClassTemplateSpecializationDecl* ctsd =
			clang::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(d))
	{
		if (!ctsd->isExplicitSpecialization())
		{
			return true;
		}
	}
	else if (const clang::FunctionDecl* fd = clang::dyn_cast_or_null<clang::FunctionDecl>(d))
	{
		if (fd->isTemplateInstantiation() &&
			fd->getTemplateSpecializationKind() !=
				clang::TSK_ExplicitSpecialization)	  // or undefined??
		{
			return true;
		}
		else if (
			fd->getMemberSpecializationInfo() &&
			fd->getMemberSpecializationInfo()->getTemplateSpecializationKind() ==
				clang::TSK_ExplicitSpecialization)
		{
			return false;
		}
	}

	return isImplicit(clang::dyn_cast_or_null<clang::Decl>(d->getDeclContext()));
}

inline DefinitionKind utility::getDefinitionKind(const clang::Decl* d)
{
	using enum DefinitionKind;
	return isImplicit(d) ? IMPLICIT : EXPLICIT;
}

inline AccessKind utility::convertAccessSpecifier(clang::AccessSpecifier access)
{
	using enum clang::AccessSpecifier;
	using enum AccessKind;
	switch (access)
	{
	case AS_public:
		return PUBLIC;
	case AS_protected:
		return PROTECTED;
	case AS_private:
		return PRIVATE;
	case AS_none:
		return NONE;
	default:
		return NONE;
	}
}

inline SymbolKind utility::convertTagKind(const clang::TagTypeKind tagKind)
{
	using enum SymbolKind;
	using enum clang::TagTypeKind;
	switch (tagKind)
	{
	case Struct:
		return STRUCT;
	case Union:
		return UNION;
	case Class:
		return CLASS;
	case Enum:
		return ENUM;
	case Interface:
		// TODO (petermost): Is this correct or should 'SymbolKind::INTERFACE' be returned?
		return UNDEFINED;
	default:
		return UNDEFINED;
	}
}

inline bool utility::isLocalVariable(const clang::ValueDecl *d)
{
	return !llvm::isa<clang::ParmVarDecl>(d) && !(d->getParentFunctionOrMethod() == nullptr);
}

inline bool utility::isParameter(const clang::ValueDecl *d)
{
	return llvm::isa<clang::ParmVarDecl>(d);
}

inline SymbolKind utility::getSymbolKind(const clang::VarDecl* d)
{
	using enum SymbolKind;
	SymbolKind symbolKind = UNDEFINED;

	if (d->getParentFunctionOrMethod() == nullptr)
	{
		using enum clang::AccessSpecifier;
		if (d->getAccess() == AS_none)
		{
			symbolKind = GLOBAL_VARIABLE;
		}
		else
		{
			symbolKind = FIELD;
		}
	}

	return symbolKind;
}

inline std::string utility::getFileNameOfFileEntry(const clang::FileEntryRef &entry)
{
	std::string fileName = entry.getFileEntry().tryGetRealPathName().str();
	if (fileName.empty())
	{
		fileName = entry.getName().str();
	}
	else
	{
		fileName = FilePath(entry.getName().str()).getParentDirectory().concatenate(FilePath(fileName).fileName()).str();
	}
	return fileName;
}

inline ParseLocation utility::getParseLocation(
	const clang::SourceLocation& sourceLocation,
	const clang::SourceManager& sourceManager,
	clang::Preprocessor* preprocessor,
	CanonicalFilePathCache& canonicalFilePathCache)
{
	if (sourceLocation.isValid())
	{
		clang::SourceLocation loc = sourceLocation;
		if (sourceManager.isMacroBodyExpansion(sourceLocation))
		{
			loc = sourceManager.getExpansionLoc(sourceLocation);
			if (loc.isInvalid())
			{
				loc = sourceLocation;
			}
		}

		const clang::SourceLocation startLoc = sourceManager.getSpellingLoc(loc);
		const clang::FileID fileId = sourceManager.getFileID(startLoc);

		// find the start location
		const unsigned int startOffset = sourceManager.getFileOffset(startLoc);

		// General case -- find the end of the token starting at loc.
		if (preprocessor != nullptr)
		{
			const clang::SourceLocation endSloc = preprocessor->getLocForEndOfToken(startLoc);
			const unsigned int endOffset = sourceManager.getFileOffset(endSloc);

			return ParseLocation(
				canonicalFilePathCache.getFileSymbolId(fileId),
				sourceManager.getLineNumber(fileId, startOffset),
				sourceManager.getColumnNumber(fileId, startOffset),
				sourceManager.getLineNumber(fileId, endOffset),
				sourceManager.getColumnNumber(fileId, endOffset) - 1);
		}
		else
		{
			return ParseLocation(
				canonicalFilePathCache.getFileSymbolId(fileId),
				sourceManager.getLineNumber(fileId, startOffset),
				sourceManager.getColumnNumber(fileId, startOffset));
		}
	}

	return ParseLocation();
}

inline ParseLocation utility::getParseLocation(
	const clang::SourceRange& sourceRange,
	const clang::SourceManager& sourceManager,
	clang::Preprocessor* preprocessor,
	CanonicalFilePathCache& canonicalFilePathCache)
{
	if (sourceRange.isValid())
	{
		clang::SourceRange range = sourceRange;
		clang::SourceLocation endLoc = preprocessor->getLocForEndOfToken(range.getEnd());

		if ((sourceManager.isMacroArgExpansion(range.getBegin()) ||
			 sourceManager.isMacroBodyExpansion(range.getBegin())) &&
			(sourceManager.isMacroArgExpansion(range.getEnd()) ||
			 sourceManager.isMacroBodyExpansion(range.getEnd())))
		{
			range = sourceManager.getExpansionRange(sourceRange).getAsRange();
			if (range.isValid())
			{
				endLoc = preprocessor->getLocForEndOfToken(range.getBegin());
			}
			else
			{
				range = sourceRange;
			}
		}

		const clang::SourceLocation beginLoc = range.getBegin();

		const clang::PresumedLoc presumedBegin = sourceManager.getPresumedLoc(beginLoc, false);
		const clang::PresumedLoc presumedEnd = sourceManager.getPresumedLoc(
			endLoc.isValid() ? endLoc : range.getEnd(), false);

		Id fileSymbolId = canonicalFilePathCache.getFileSymbolId(sourceManager.getFileID(beginLoc));
		if (!fileSymbolId)
		{
			fileSymbolId = canonicalFilePathCache.getFileSymbolId(presumedBegin.getFilename());
		}

		return ParseLocation(
			fileSymbolId,
			presumedBegin.getLine(),
			presumedBegin.getColumn(),
			presumedEnd.getLine(),
			presumedEnd.getColumn() - (endLoc.isValid() ? 1 : 0));
	}

	return ParseLocation();
}

inline clang::PrintingPolicy utility::makePrintingPolicyForCPlusPlus()
{
	clang::PrintingPolicy pp = clang::PrintingPolicy(clang::LangOptions());
	pp.adjustForCPlusPlus();

	return pp;
}

inline std::optional<std::filesystem::path> utility::resolveCompilerResourceDir(const std::filesystem::path& compilerPath)
{
	static std::mutex cacheMutex;
	static std::map<std::string, std::optional<std::filesystem::path>> cache;
	
	// When no compiler is known, probe the system default so macOS shims
	// (e.g. /usr/bin/c++) resolve to the real Xcode toolchain resource dir.
	const std::string compilerPathStr =
		compilerPath.empty() ? ClangCompiler::DEFAULT_COMPILER : compilerPath.string();
	
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		auto it = cache.find(compilerPathStr);
		if (it != cache.end())
		{
			return it->second;
		}
	}
	
	std::optional<std::filesystem::path> result = std::nullopt;

	// 1. Try to invoke the compiler directly to ask for its resource dir.
	// This correctly resolves Apple shims like /usr/bin/c++ to their true
	// Xcode toolchain paths, which GetResourcesPath cannot deduce.
	const utility::ProcessOutput output = utility::executeProcess(compilerPathStr, {"-print-resource-dir"});
	if (output.exitCode == 0 && !output.output.empty())
	{
		std::filesystem::path dir(utility::trim(output.output));
		if (std::filesystem::exists(dir))
		{
			result = dir;
		}
	}
	
	// 2. Fallback to Clang's path deduction logic if direct invocation fails
	// (e.g., if the compiler is missing the -print-resource-dir flag).
	if (!result)
	{
		std::filesystem::path dir{clang_compat::getResourcesPath(compilerPathStr)};
		if (std::filesystem::exists(dir))
		{
			result = dir;
		}
	}
	
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		cache[compilerPathStr] = result;
	}

	return result;
}
