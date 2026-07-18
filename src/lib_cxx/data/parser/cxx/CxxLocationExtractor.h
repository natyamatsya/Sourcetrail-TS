#ifndef CXX_LOCATION_EXTRACTOR_H
#define CXX_LOCATION_EXTRACTOR_H

#include <clang/Basic/SourceLocation.h>

#include "ParseLocation.h"

namespace clang
{
class ASTContext;
class Preprocessor;
class TagDecl;
class FunctionDecl;
}

class CanonicalFilePathCache;

// Turns Clang source locations / declarations into Sourcetrail ParseLocations. Extracted so
// location computation is a single first-class concern instead of being smeared across the
// visitor and its components (which previously forwarded to it). Borrows its inputs, all owned
// for the duration of the parse.
class CxxLocationExtractor
{
public:
	CxxLocationExtractor(
		clang::ASTContext& astContext,
		clang::Preprocessor* preprocessor,
		CanonicalFilePathCache& canonicalFilePathCache);

	ParseLocation getParseLocation(const clang::SourceLocation& loc) const;
	ParseLocation getParseLocation(const clang::SourceRange& range) const;

	ParseLocation getParseLocationOfTagDeclBody(clang::TagDecl* decl) const;
	ParseLocation getParseLocationOfFunctionBody(const clang::FunctionDecl* decl) const;
	ParseLocation getSignatureLocation(clang::FunctionDecl* decl) const;

private:
	clang::ASTContext& m_astContext;
	clang::Preprocessor* m_preprocessor;
	CanonicalFilePathCache& m_canonicalFilePathCache;
};

#endif	  // CXX_LOCATION_EXTRACTOR_H
