#ifndef CXX_CONCEPT_REFERENCE_RECORDER_H
#define CXX_CONCEPT_REFERENCE_RECORDER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <llvm/ADT/ArrayRef.h>

namespace clang
{
class ASTContext;
class ConceptDecl;
class ConceptReference;
class ConceptSpecializationExpr;
class Decl;
class Expr;
class FunctionDecl;
class QualType;
class SourceLocation;
class TemplateArgument;
class TemplateDecl;
class TemplateSpecializationType;
class VarTemplateSpecializationDecl;
class ClassTemplateSpecializationDecl;
}

class CanonicalFilePathCache;
class ParserClient;
#endif

SRCTRL_EXPORT class CxxSymbolRegistry;
SRCTRL_EXPORT class CxxLocationExtractor;
SRCTRL_EXPORT class CxxAstVisitorComponentContext;

// Records C++20 concept-constraint references (on template parameters, on abbreviated-template /
// auto parameters, ...) as usage edges from the current context to the concept, and which types
// satisfy which concepts (EDGE_SATISFIES, type -> concept), as far as the code proves it.
// Extracted from CxxAstVisitorComponentIndexer so the concept-recording logic stands on its own;
// constructed on demand at the (rare) visit sites that need it, borrowing the collaborators it
// uses.
SRCTRL_EXPORT class CxxConceptReferenceRecorder
{
public:
	CxxConceptReferenceRecorder(
		clang::ASTContext& astContext,
		ParserClient& client,
		CanonicalFilePathCache& canonicalFilePathCache,
		CxxSymbolRegistry& symbols,
		CxxLocationExtractor& locations,
		CxxAstVisitorComponentContext& context);

	void recordTemplateParameterConceptReferences(const clang::TemplateDecl* templateDecl);

	// Records the concept reference carried by `d`, if any. T is any Clang node exposing
	// getConceptReference() (clang::TypeConstraint, clang::AutoType, ...).
	template <typename T>
	void recordConceptReference(const T* d)
	{
		if (const clang::ConceptReference* conceptReference = d->getConceptReference())
		{
			recordNamedConceptReference(conceptReference);
		}
	}

	void recordNamedConceptReference(const clang::ConceptReference* conceptReference);

	// --- satisfaction --------------------------------------------------------------------------
	// A specialization of a constrained template exists only if its arguments satisfied the
	// template's constraints: each concept-id in their conjunction holds for the argument it
	// constrains. `useLocation` is where the code names the specialization; the edge is located
	// at its point of instantiation when that is in a project file, else there.
	void recordSatisfiedConstraints(const clang::FunctionDecl* specialization, const clang::SourceLocation& useLocation);
	void recordSatisfiedConstraints(
		const clang::ClassTemplateSpecializationDecl* specialization, const clang::SourceLocation& useLocation);
	void recordSatisfiedConstraints(
		const clang::VarTemplateSpecializationDecl* specialization, const clang::SourceLocation& useLocation);
	// An alias template's specialization, its arguments as written.
	void recordSatisfiedConstraints(
		const clang::TemplateSpecializationType* aliasSpecialization, const clang::SourceLocation& useLocation);
	// A concept-id clang evaluated (static_assert(Shape<Circle>), an if constexpr in an
	// instantiation): recorded when it holds.
	void recordSatisfiedConceptSpecialization(const clang::ConceptSpecializationExpr* conceptSpecialization);

private:
	void recordSatisfiedConstraints(
		const clang::TemplateDecl* templateDecl,
		llvm::ArrayRef<clang::TemplateArgument> arguments,
		const clang::SourceLocation& location);
	void recordSatisfiedConstraint(
		const clang::Expr* constraint,
		unsigned depth,
		llvm::ArrayRef<clang::TemplateArgument> arguments,
		const clang::SourceLocation& location);
	void recordSatisfaction(
		clang::QualType type, const clang::ConceptDecl* conceptDecl, const clang::SourceLocation& location);
	bool isInProjectFile(const clang::Decl* decl);
	clang::SourceLocation recordedLocation(
		const clang::SourceLocation& pointOfInstantiation, const clang::SourceLocation& useLocation);

	clang::ASTContext& m_astContext;
	ParserClient& m_client;
	CanonicalFilePathCache& m_canonicalFilePathCache;
	CxxSymbolRegistry& m_symbols;
	CxxLocationExtractor& m_locations;
	CxxAstVisitorComponentContext& m_context;
};


// NOTE: no classic bottom-include here -- this header is top-included by other blob headers, so a
// bottom-include of the apex could fire while an includer's class is still incomplete. Classic
// consumers reach the inline bodies through any converging blob header (see CxxAstVisitorBodies.h).

#endif	  // CXX_CONCEPT_REFERENCE_RECORDER_H
