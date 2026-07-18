#ifndef CXX_DESTRUCTOR_CALL_RECORDER_H
#define CXX_DESTRUCTOR_CALL_RECORDER_H

namespace clang
{
class ASTContext;
class FunctionDecl;
}

class ParserClient;
class CxxSymbolRegistry;
class CxxLocationExtractor;
class CxxAstVisitorComponentContext;

// Records the implicit and base-class destructor calls the compiler inserts at the end of a
// function body, as CALL edges. Unlike the rest of the indexer (which visits AST nodes), this
// builds a Clang CFG and walks its control-flow blocks to find those destructors. Extracted so
// that this control-flow analysis lives on its own rather than inside the AST visitor.
class CxxDestructorCallRecorder
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

#endif	  // CXX_DESTRUCTOR_CALL_RECORDER_H
