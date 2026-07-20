#ifndef CXX_AST_VISITOR_COMPONENT_BRACE_RECORDER_H
#define CXX_AST_VISITOR_COMPONENT_BRACE_RECORDER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitorComponent.h"
#include "ParseLocation.h"

class ParserClient;
#endif

SRCTRL_EXPORT class CxxAstVisitorComponentContext;
SRCTRL_EXPORT class CxxLocationExtractor;

// This CxxAstVisitorComponent is responsible for recording all matching braces ["{", "}"]
// throughout the visited AST.
SRCTRL_EXPORT class CxxAstVisitorComponentBraceRecorder: public CxxAstVisitorComponent
{
public:
	CxxAstVisitorComponentBraceRecorder(
		CxxAstVisitor* astVisitor, clang::ASTContext* astContext, ParserClient& client);

	void wire();

	void visitTagDecl(clang::TagDecl* d);
	void visitNamespaceDecl(clang::NamespaceDecl* d);
	void visitCompoundStmt(clang::CompoundStmt* s);
	void visitInitListExpr(clang::InitListExpr* s);
	void visitMSAsmStmt(clang::MSAsmStmt* s);

private:
	ParseLocation getParseLocation(const clang::SourceLocation& loc) const;
	FilePath getFilePath(const clang::SourceLocation& loc);

	void recordBraces(
		const FilePath& filePath, const ParseLocation& lbraceLoc, const ParseLocation& rbraceLoc);
	clang::SourceLocation getFirstLBraceLocation(
		clang::SourceLocation searchStartLoc, clang::SourceLocation searchEndLoc) const;
	clang::SourceLocation getLastRBraceLocation(
		clang::SourceLocation searchStartLoc, clang::SourceLocation searchEndLoc) const;

	clang::ASTContext* m_astContext;
	ParserClient& m_client;
	CxxLocationExtractor& m_locations;
	CxxAstVisitorComponentContext* m_context = nullptr;
};


// Classic build: converge on the family apex, whose bottom includes all visitor-blob bodies once
// every class definition is complete (see CxxAstVisitorBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitor.h"
#endif

#endif	  // CXX_AST_VISITOR_COMPONENT_BRACE_RECORDER_H
