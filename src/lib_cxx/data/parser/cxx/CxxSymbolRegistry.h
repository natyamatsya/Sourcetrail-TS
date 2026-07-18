#ifndef CXX_SYMBOL_REGISTRY_H
#define CXX_SYMBOL_REGISTRY_H

#include <map>

#include "CxxContext.h"
#include "types.h"

class CanonicalFilePathCache;
class NameHierarchy;
class ParserClient;

// Interns AST nodes as Sourcetrail symbols: resolves a decl's/type's qualified name, records it
// once through the ParserClient, and caches the resulting symbol Id. Extracted from
// CxxAstVisitorComponentIndexer so symbol identity is a standalone, independently testable concern
// with a narrow dependency surface (the client + the canonical-file-path cache).
class CxxSymbolRegistry
{
public:
	CxxSymbolRegistry(ParserClient& client, CanonicalFilePathCache& canonicalFilePathCache);

	Id getOrCreateSymbolId(const clang::NamedDecl* decl);
	Id getOrCreateSymbolId(const clang::Type* type);
	Id getOrCreateSymbolId(CxxContext context);
	Id getOrCreateSymbolId(CxxContext context, const NameHierarchy& fallback);

private:
	ParserClient& m_client;
	CanonicalFilePathCache& m_canonicalFilePathCache;

	std::map<const clang::NamedDecl*, Id> m_declSymbolIds;
	std::map<const clang::Type*, Id> m_typeSymbolIds;
};

#endif	  // CXX_SYMBOL_REGISTRY_H
