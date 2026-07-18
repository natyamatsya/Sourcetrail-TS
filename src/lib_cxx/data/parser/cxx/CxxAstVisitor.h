#ifndef CXX_AST_VISITOR_H
#define CXX_AST_VISITOR_H

#include <concepts>
#include <memory>
#include <string>
#include <tuple>

#pragma warning(push)
#pragma warning(disable: 4702) // unreachable code
#include <clang/AST/RecursiveASTVisitor.h>
#pragma warning(pop)

#include "CxxAstVisitorComponent.h"
#include "CxxAstVisitorComponentBraceRecorder.h"
#include "CxxAstVisitorComponentContext.h"
#include "CxxAstVisitorComponentDeclRefKind.h"
#include "CxxAstVisitorComponentImplicitCode.h"
#include "CxxAstVisitorComponentIndexer.h"
#include "CxxAstVisitorComponentTypeRefKind.h"
#include "CxxIndexingContext.h"
#include "CxxLocationExtractor.h"
#include "FilePath.h"

class CanonicalFilePathCache;
class ParserClient;
class FilePath;

struct IndexerStateInfo;
struct ParseLocation;

// methods are called in this order:
//	TraverseDecl()
//	`-	TraverseFunctionDecl()
// 		|-	WalkUpFromFunctionDecl()
// 		|	|-	WalkUpFromNamedDecl()
// 		|	|	|-	WalkUpFromDecl()
//	 	|	|	|	`-	VisitDecl()
// 		|	|	`-	VisitNamedDecl()
// 		|	`-	VisitFunctionDecl()
// 		`-	TraverseChildNodes()

// A CxxAstVisitor component hooks into the traversal by (optionally) overriding the
// begin-/end-/visit methods that CxxAstVisitorComponent declares. This concept captures
// exactly that contract, so the component set is checked where it is declared below.
template <class T>
concept CxxAstVisitorComponentC = std::derived_from<T, CxxAstVisitorComponent>;

// The registered components, held by value. Adding a component is a single entry here (plus
// its constructor argument); CxxAstVisitor::forEachComponent then fans out to it automatically.
template <CxxAstVisitorComponentC... Components>
using CxxAstVisitorComponents = std::tuple<Components...>;

// This is a "curiously recurring template pattern (CRTP)" visitor, so it needs no virtual
// functions. Verbose AST logging (formerly the virtual 'CxxVerboseAstVisitor' subclass) is now an
// opt-in mode selected at construction: when isVerbose is set, the Traverse* methods log each
// visited node before delegating to the normal traversal.
class CxxAstVisitor: public clang::RecursiveASTVisitor<CxxAstVisitor>
{
public:
	CxxAstVisitor(
		clang::ASTContext* astContext,
		clang::Preprocessor* preprocessor,
		ParserClient& client,
		CanonicalFilePathCache& canonicalFilePathCache,
		std::shared_ptr<IndexerStateInfo> indexerStateInfo,
		bool isVerbose = false);
	~CxxAstVisitor() = default;

	CxxAstVisitorComponentDeclRefKind *getDeclRefKindComponent();

	CxxAstVisitorComponentTypeRefKind *getTypeRefKindComponent();

	CxxAstVisitorComponentContext *getContextComponent();

	CanonicalFilePathCache* getCanonicalFilePathCache() const;

	// Indexing entry point
	void indexDecl(clang::Decl* d);

	// Visitor options
	bool shouldVisitTemplateInstantiations() const;
	bool shouldVisitImplicitCode() const;

	static bool shouldHandleTypeLoc(const clang::TypeLoc& tl);

	// Traversal methods. These specify how to traverse the AST and record context info.
	bool TraverseDecl(clang::Decl* d);
	bool TraverseQualifiedTypeLoc(clang::QualifiedTypeLoc tl, bool TraverseQualifier = true);
	bool TraverseTypeLoc(clang::TypeLoc tl, bool TraverseQualifier = true);
	bool TraverseType(clang::QualType t, bool TraverseQualifier = true);
	bool TraverseStmt(clang::Stmt* stmt);

	bool TraverseCXXRecordDecl(clang::CXXRecordDecl* d);
	bool traverseCXXBaseSpecifier(const clang::CXXBaseSpecifier& d);
	bool TraverseCXXMethodDecl(clang::CXXMethodDecl* d);
	bool TraverseTemplateTypeParmDecl(clang::TemplateTypeParmDecl* d);
	bool TraverseTemplateTemplateParmDecl(clang::TemplateTemplateParmDecl* d);
	bool TraverseNestedNameSpecifierLoc(clang::NestedNameSpecifierLoc loc);
	bool TraverseConstructorInitializer(clang::CXXCtorInitializer* init);
	bool TraverseCallExpr(clang::CallExpr* s);
	bool TraverseCXXMemberCallExpr(clang::CXXMemberCallExpr* s);
	bool TraverseCXXOperatorCallExpr(clang::CXXOperatorCallExpr* s);
	bool TraverseCXXConstructExpr(clang::CXXConstructExpr* s);
	bool TraverseCXXTemporaryObjectExpr(clang::CXXTemporaryObjectExpr* s);
	bool TraverseLambdaExpr(clang::LambdaExpr* s);
	bool TraverseFunctionDecl(clang::FunctionDecl* d);
	bool TraverseClassTemplateSpecializationDecl(clang::ClassTemplateSpecializationDecl* d);
	bool TraverseClassTemplatePartialSpecializationDecl(clang::ClassTemplatePartialSpecializationDecl* d);
	bool TraverseDeclRefExpr(clang::DeclRefExpr* s);
	bool TraverseCXXForRangeStmt(clang::CXXForRangeStmt* s);
	bool TraverseTemplateSpecializationTypeLoc(
		clang::TemplateSpecializationTypeLoc loc, bool TraverseQualifier = true);
	bool TraverseUnresolvedLookupExpr(clang::UnresolvedLookupExpr* s);
	bool TraverseUnresolvedMemberExpr(clang::UnresolvedMemberExpr* S);
	bool TraverseTemplateArgumentLoc(const clang::TemplateArgumentLoc& loc);
	bool TraverseLambdaCapture(clang::LambdaExpr* lambdaExpr, const clang::LambdaCapture* capture, clang::Expr* Init);
	bool TraverseBinComma(clang::BinaryOperator* s);

	static bool TraverseDeclarationNameInfo(clang::DeclarationNameInfo NameInfo);

#define OPERATOR(NAME)                                                                             \
	bool TraverseBin##NAME##Assign(clang::CompoundAssignOperator* s)                       \
	{                                                                                              \
		return TraverseAssignCommon(s);                                                            \
	}
	OPERATOR(Mul)
	OPERATOR(Div)
	OPERATOR(Rem)
	OPERATOR(Add)
	OPERATOR(Sub)
	OPERATOR(Shl)
	OPERATOR(Shr)
	OPERATOR(And)
	OPERATOR(Or)
	OPERATOR(Xor)
#undef OPERATOR


	void traverseDeclContextHelper(clang::DeclContext* d);
	bool TraverseCallCommon(clang::CallExpr* s);
	bool TraverseAssignCommon(clang::BinaryOperator* s);

	// Visitor methods. These actually record stuff and store it in the database.
	bool VisitCastExpr(clang::CastExpr* s);
	bool VisitCXXFunctionalCastExpr(clang::CXXFunctionalCastExpr *s);
	bool VisitUnaryAddrOf(clang::UnaryOperator* s);
	bool VisitUnaryDeref(clang::UnaryOperator* s);
	bool VisitDeclStmt(clang::DeclStmt* s);
	bool VisitReturnStmt(clang::ReturnStmt* s);
	bool VisitCompoundStmt(clang::CompoundStmt* s);
	bool VisitInitListExpr(clang::InitListExpr* s);


	bool VisitTagDecl(clang::TagDecl* d);
	bool VisitClassTemplateDecl(clang::ClassTemplateDecl *d);
	bool VisitClassTemplateSpecializationDecl(clang::ClassTemplateSpecializationDecl* d);
	bool VisitFunctionDecl(clang::FunctionDecl* d);
	bool VisitFunctionTemplateDecl(clang::FunctionTemplateDecl *d);
	bool VisitCXXMethodDecl(clang::CXXMethodDecl* d);
	bool VisitVarDecl(clang::VarDecl* d);
	bool VisitDecompositionDecl(clang::DecompositionDecl *d);
	bool VisitVarTemplateSpecializationDecl(clang::VarTemplateSpecializationDecl* d);
	bool VisitFieldDecl(clang::FieldDecl* d);
	bool VisitTypedefDecl(clang::TypedefDecl* d);
	bool VisitTypeAliasDecl(clang::TypeAliasDecl* d);
	bool VisitNamespaceDecl(clang::NamespaceDecl* d);
	bool VisitNamespaceAliasDecl(clang::NamespaceAliasDecl* d);
	bool VisitEnumConstantDecl(clang::EnumConstantDecl* d);
	bool VisitUsingDirectiveDecl(clang::UsingDirectiveDecl* d);
	bool VisitUsingDecl(clang::UsingDecl* d);
	bool VisitNonTypeTemplateParmDecl(clang::NonTypeTemplateParmDecl* d);
	bool VisitTemplateTypeParmDecl(clang::TemplateTypeParmDecl* d);
	bool VisitTemplateTemplateParmDecl(clang::TemplateTemplateParmDecl* d);
	bool VisitConceptDecl(clang::ConceptDecl *d);
	bool VisitConceptSpecializationExpr(clang::ConceptSpecializationExpr *d);
	bool VisitConceptReference(clang::ConceptReference *d);
	static bool VisitTranslationUnitDecl(clang::TranslationUnitDecl* d);

	bool VisitTypeLoc(clang::TypeLoc tl);

	bool VisitDeclRefExpr(clang::DeclRefExpr* s);
	bool VisitMemberExpr(clang::MemberExpr* s);
	bool VisitCXXDependentScopeMemberExpr(clang::CXXDependentScopeMemberExpr* s);
	bool VisitCXXConstructExpr(clang::CXXConstructExpr* s);
	bool VisitCXXDeleteExpr(clang::CXXDeleteExpr* s);
	bool VisitLambdaExpr(clang::LambdaExpr* s);
	bool VisitMSAsmStmt(clang::MSAsmStmt* s);
	bool VisitConstructorInitializer(clang::CXXCtorInitializer* init);

	CxxLocationExtractor& getLocationExtractor();

	bool shouldVisitStmt(const clang::Stmt* stmt) const;
	bool shouldVisitDecl(const clang::Decl* decl) const;
	bool shouldVisitReference(const clang::SourceLocation& referenceLocation) const;

	bool isLocatedInProjectFile(clang::SourceLocation loc) const;

protected:
	typedef clang::RecursiveASTVisitor<CxxAstVisitor> Base;

	clang::ASTContext* m_astContext;
	clang::Preprocessor* m_preprocessor;
	ParserClient& m_client;
	std::shared_ptr<IndexerStateInfo> m_indexerStateInfo;
	CanonicalFilePathCache& m_canonicalFilePathCache;

	// Verbose AST-logging mode (see the class comment). When off, the log helpers are no-ops and
	// the indentation counter is unused.
	void logVerboseDecl(clang::Decl* d);
	void logVerboseStmt(clang::Stmt* stmt);
	void logVerboseTypeLoc(clang::TypeLoc tl);
	std::string getIndentString() const;

	bool m_isVerbose;
	unsigned int m_indentation = 0;
	FilePath m_currentFilePath;

	CxxLocationExtractor m_locations;

	// The shared mid-level indexing API handed to the indexing components. Declared after
	// m_locations (which it borrows) and before m_components (whose indexer borrows it); its
	// context collaborator is wired in the constructor body once the tuple exists.
	CxxIndexingContext m_index;

	// Invoke `function` on every registered component, in registration order.
	template <class F>
	void forEachComponent(F&& function)
	{
		std::apply([&](auto&... components) { (function(components), ...); }, m_components);
	}

	// Runs a node's base RecursiveASTVisitor traversal wrapped in begin/end notifications to
	// every component. `baseTraversal` performs the Base::Traverse* call. These pass-through
	// overrides never abort the traversal, so this always returns true.
	template <class BeginHook, class BaseTraversal, class EndHook>
	bool traverseWithComponents(BeginHook beginHook, BaseTraversal baseTraversal, EndHook endHook)
	{
		forEachComponent(beginHook);
		baseTraversal();
		forEachComponent(endHook);
		return true;
	}

	CxxAstVisitorComponents<
		CxxAstVisitorComponentContext,
		CxxAstVisitorComponentTypeRefKind,
		CxxAstVisitorComponentDeclRefKind,
		CxxAstVisitorComponentImplicitCode,
		CxxAstVisitorComponentIndexer,
		CxxAstVisitorComponentBraceRecorder>
		m_components;
};


#endif	  // CXX_AST_VISITOR_H
