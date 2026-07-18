#ifndef CXX_CONCEPT_REFERENCE_RECORDER_H
#define CXX_CONCEPT_REFERENCE_RECORDER_H

namespace clang
{
class ConceptReference;
class TemplateDecl;
}

class ParserClient;
class CxxSymbolRegistry;
class CxxLocationExtractor;
class CxxAstVisitorComponentContext;

// Records C++20 concept-constraint references (on template parameters, on abbreviated-template /
// auto parameters, ...) as usage edges from the current context to the concept. Extracted from
// CxxAstVisitorComponentIndexer so the concept-recording logic stands on its own; constructed on
// demand at the (rare) visit sites that need it, borrowing the collaborators it uses.
class CxxConceptReferenceRecorder
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

#endif	  // CXX_CONCEPT_REFERENCE_RECORDER_H
