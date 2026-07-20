#ifndef CXX_AST_VISITOR_COMPONENT_DECLARATION_INDEXER_H
#define CXX_AST_VISITOR_COMPONENT_DECLARATION_INDEXER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitorComponent.h"
#endif

SRCTRL_EXPORT class CxxIndexingContext;

// Records the symbols the AST declares: one visit hook per declaration kind. Each hook resolves the
// declaration's symbol id and its attributes (kind, access, definition, location, deprecation)
// through the shared CxxIndexingContext -- most of that boilerplate lives in the facade's
// recordDeclaration idiom. Split out of the former monolithic indexer so declaration recording is a
// standalone concern, sitting alongside the reference and type-usage indexers.
SRCTRL_EXPORT class CxxAstVisitorComponentDeclarationIndexer: public CxxAstVisitorComponent
{
public:
	CxxAstVisitorComponentDeclarationIndexer(CxxAstVisitor* astVisitor, CxxIndexingContext& index);

	void visitTagDecl(clang::TagDecl* d);
	void visitClassTemplateDecl(clang::ClassTemplateDecl* d);
	void visitClassTemplateSpecializationDecl(clang::ClassTemplateSpecializationDecl* d);
	void visitVarDecl(clang::VarDecl* d);
	void visitDecompositionDecl(clang::DecompositionDecl* d);
	void visitVarTemplateSpecializationDecl(clang::VarTemplateSpecializationDecl* d);
	void visitFieldDecl(clang::FieldDecl* d);
	void visitFunctionDecl(clang::FunctionDecl* d);
	void visitFunctionTemplateDecl(clang::FunctionTemplateDecl* d);
	void visitCXXMethodDecl(clang::CXXMethodDecl* d);
	void visitEnumConstantDecl(clang::EnumConstantDecl* d);
	void visitNamespaceDecl(clang::NamespaceDecl* d);
	void visitNamespaceAliasDecl(clang::NamespaceAliasDecl* d);
	void visitTypedefDecl(clang::TypedefDecl* d);
	void visitTypeAliasDecl(clang::TypeAliasDecl* d);
	void visitUsingDirectiveDecl(clang::UsingDirectiveDecl* d);
	void visitUsingDecl(clang::UsingDecl* d);
	void visitNonTypeTemplateParmDecl(clang::NonTypeTemplateParmDecl* d);
	void visitTemplateTypeParmDecl(clang::TemplateTypeParmDecl* d);
	void visitTemplateTemplateParmDecl(clang::TemplateTemplateParmDecl* d);
	void visitConceptDecl(clang::ConceptDecl* d);

private:
	// The mid-level indexing API: symbol identity, locations, the storage client, and the current
	// context, plus the recordDeclaration / recordReference idioms. Owned by CxxAstVisitor.
	CxxIndexingContext& m_index;
};


// Classic build: converge on the family apex, whose bottom includes all visitor-blob bodies once
// every class definition is complete (see CxxAstVisitorBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitor.h"
#endif

#endif	  // CXX_AST_VISITOR_COMPONENT_DECLARATION_INDEXER_H
