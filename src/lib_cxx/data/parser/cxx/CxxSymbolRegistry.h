#ifndef CXX_SYMBOL_REGISTRY_H
#define CXX_SYMBOL_REGISTRY_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <map>

#include "CxxContext.h"
#include "types.h"

class CanonicalFilePathCache;
class NameHierarchy;
class ParserClient;
#endif

// Interns AST nodes as Sourcetrail symbols: resolves a decl's/type's qualified name, records it
// once through the ParserClient, and caches the resulting symbol Id. Extracted from
// CxxAstVisitorComponentIndexer so symbol identity is a standalone, independently testable concern
// with a narrow dependency surface (the client + the canonical-file-path cache).
SRCTRL_EXPORT class CxxSymbolRegistry
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


// NOTE: no classic bottom-include here -- this header is top-included by other blob headers, so a
// bottom-include of the apex could fire while an includer's class is still incomplete. Classic
// consumers reach the inline bodies through any converging blob header (see CxxAstVisitorBodies.h).

#endif	  // CXX_SYMBOL_REGISTRY_H
