#ifndef CXX_CONCEPT_REFERENCE_RECORDER_H
#define CXX_CONCEPT_REFERENCE_RECORDER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
namespace clang
{
class ConceptReference;
class TemplateDecl;
}

class ParserClient;
#endif

SRCTRL_EXPORT class CxxSymbolRegistry;
SRCTRL_EXPORT class CxxLocationExtractor;
SRCTRL_EXPORT class CxxAstVisitorComponentContext;

// Records C++20 concept-constraint references (on template parameters, on abbreviated-template /
// auto parameters, ...) as usage edges from the current context to the concept. Extracted from
// CxxAstVisitorComponentIndexer so the concept-recording logic stands on its own; constructed on
// demand at the (rare) visit sites that need it, borrowing the collaborators it uses.
SRCTRL_EXPORT class CxxConceptReferenceRecorder
{
public:
	CxxConceptReferenceRecorder(
		ParserClient& client,
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

private:
	ParserClient& m_client;
	CxxSymbolRegistry& m_symbols;
	CxxLocationExtractor& m_locations;
	CxxAstVisitorComponentContext& m_context;
};


// NOTE: no classic bottom-include here -- this header is top-included by other blob headers, so a
// bottom-include of the apex could fire while an includer's class is still incomplete. Classic
// consumers reach the inline bodies through any converging blob header (see CxxAstVisitorBodies.h).

#endif	  // CXX_CONCEPT_REFERENCE_RECORDER_H
