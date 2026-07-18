#ifndef CXX_INDEXING_CONTEXT_H
#define CXX_INDEXING_CONTEXT_H

#include <string>

#include "AccessKind.h"
#include "CxxConceptReferenceRecorder.h"
#include "CxxContext.h"
#include "CxxDestructorCallRecorder.h"
#include "CxxSymbolRegistry.h"
#include "DefinitionKind.h"
#include "NodeAttributeKind.h"
#include "NodeModifier.h"
#include "ParseLocation.h"
#include "ReferenceKind.h"
#include "SymbolKind.h"
#include "types.h"

namespace clang
{
class ASTContext;
class Decl;
class NamedDecl;
class Type;
class DeducedType;
class QualType;
class MemberSpecializationInfo;
class SourceLocation;
class SourceRange;
class TagDecl;
class FunctionDecl;
}

class ParserClient;
class CanonicalFilePathCache;
class CxxLocationExtractor;
class CxxAstVisitorComponentContext;
class NameHierarchy;

// The mid-level indexing API that sits between the low-level ParserClient storage sink and the
// per-node-kind handlers. It bundles the four collaborators every handler needs -- symbol identity
// (owns the CxxSymbolRegistry), source locations, the storage client, and the current traversal
// context -- and exposes both the recording primitives and the two dominant idioms
// (recordDeclaration / recordReference). Handlers hold a single reference to this instead of
// wiring up four dependencies and repeating the plumbing.
class CxxIndexingContext
{
public:
	CxxIndexingContext(
		clang::ASTContext& astContext,
		ParserClient& client,
		CanonicalFilePathCache& canonicalFilePathCache,
		CxxLocationExtractor& locations);

	// The context component is only safely reachable after the whole component tuple is built, so
	// it is wired in separately (see CxxAstVisitor's constructor).
	void setContext(CxxAstVisitorComponentContext& context);

	// --- high-level idioms -------------------------------------------------------------------
	// Records `d` as a symbol of `kind` plus its standard attributes (access, definition kind,
	// token location, deprecation) and returns its symbol id.
	Id recordDeclaration(const clang::NamedDecl* d, SymbolKind kind);
	// Records a reference edge from the current context to `referenced` at `location`.
	Id recordReference(const clang::NamedDecl* referenced, ReferenceKind kind, const clang::SourceLocation& location);

	// --- symbol identity ---------------------------------------------------------------------
	Id getOrCreateSymbolId(const clang::NamedDecl* decl);
	Id getOrCreateSymbolId(const clang::Type* type);
	Id getOrCreateSymbolId(CxxContext context);
	Id getOrCreateSymbolId(CxxContext context, const NameHierarchy& fallback);

	// --- current context ---------------------------------------------------------------------
	CxxContext getContext(size_t skip = 0) const;
	const clang::NamedDecl* getTopmostContextDecl(size_t skip = 0) const;
	Id contextSymbolId();

	// --- locations ---------------------------------------------------------------------------
	ParseLocation getParseLocation(const clang::SourceLocation& loc) const;
	ParseLocation getParseLocation(const clang::SourceRange& range) const;
	ParseLocation getParseLocationOfTagDeclBody(clang::TagDecl* decl) const;
	ParseLocation getParseLocationOfFunctionBody(const clang::FunctionDecl* decl) const;
	ParseLocation getSignatureLocation(clang::FunctionDecl* decl) const;
	std::string getLocalSymbolName(const clang::SourceLocation& loc) const;

	// --- recording primitives (pass-through to the ParserClient) -----------------------------
	void recordSymbolKind(Id symbolId, SymbolKind symbolKind);
	void recordAccessKind(Id symbolId, AccessKind accessKind);
	void recordDefinitionKind(Id symbolId, DefinitionKind definitionKind);
	void recordNodeModifier(Id symbolId, NodeModifierMask modifier);
	void recordNodeAttribute(Id symbolId, NodeAttributeKind key, const std::string& value);
	Id recordReference(ReferenceKind kind, Id referencedSymbolId, Id contextSymbolId, const ParseLocation& location);
	void recordLocalSymbol(const std::string& name, const ParseLocation& location);
	void recordLocation(Id elementId, const ParseLocation& location, ParseLocationType type);

	// --- specialized recorders ---------------------------------------------------------------
	void recordDeprecation(Id symbolId, const clang::Decl* d);
	void recordDeducedType(const clang::DeducedType* deducedType, Id contextSymbolId, const ParseLocation& keywordLocation);
	void recordDeducedQualType(clang::QualType deducedQualType, Id contextSymbolId, const ParseLocation& keywordLocation);
	void recordTemplateMemberSpecialization(
		const clang::MemberSpecializationInfo* memberSpecializationInfo,
		Id contextId,
		const ParseLocation& location,
		SymbolKind symbolKind);

	CxxConceptReferenceRecorder concepts();
	CxxDestructorCallRecorder destructorCalls();

private:
	clang::ASTContext& m_astContext;
	ParserClient& m_client;
	CanonicalFilePathCache& m_canonicalFilePathCache;
	CxxLocationExtractor& m_locations;
	CxxAstVisitorComponentContext* m_context = nullptr;

	CxxSymbolRegistry m_symbols;
};

#endif	  // CXX_INDEXING_CONTEXT_H
