#ifndef CXX_DESTRUCTOR_CALL_RECORDER_H
#define CXX_DESTRUCTOR_CALL_RECORDER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
namespace clang
{
class ASTContext;
class FunctionDecl;
}

class ParserClient;
#endif

SRCTRL_EXPORT class CxxSymbolRegistry;
SRCTRL_EXPORT class CxxLocationExtractor;
SRCTRL_EXPORT class CxxAstVisitorComponentContext;

// Records the implicit and base-class destructor calls the compiler inserts at the end of a
// function body, as CALL edges. Unlike the rest of the indexer (which visits AST nodes), this
// builds a Clang CFG and walks its control-flow blocks to find those destructors. Extracted so
// that this control-flow analysis lives on its own rather than inside the AST visitor.
SRCTRL_EXPORT class CxxDestructorCallRecorder
{
public:
	CxxDestructorCallRecorder(
		clang::ASTContext& astContext,
		ParserClient& client,
		CxxSymbolRegistry& symbols,
		CxxLocationExtractor& locations,
		CxxAstVisitorComponentContext& context);

	void record(const clang::FunctionDecl* functionDecl);

private:
	clang::ASTContext& m_astContext;
	ParserClient& m_client;
	CxxSymbolRegistry& m_symbols;
	CxxLocationExtractor& m_locations;
	CxxAstVisitorComponentContext& m_context;
};


// NOTE: no classic bottom-include here -- this header is top-included by other blob headers, so a
// bottom-include of the apex could fire while an includer's class is still incomplete. Classic
// consumers reach the inline bodies through any converging blob header (see CxxAstVisitorBodies.h).

#endif	  // CXX_DESTRUCTOR_CALL_RECORDER_H
