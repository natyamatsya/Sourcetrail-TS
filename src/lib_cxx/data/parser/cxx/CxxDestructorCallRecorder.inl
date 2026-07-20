// Inline implementations for CxxDestructorCallRecorder.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>
#include <optional>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Type.h>
#include <clang/Analysis/CFG.h>
#include "CxxAstVisitorComponentContext.h"
#include "CxxLocationExtractor.h"
#include "CxxSymbolRegistry.h"
#include "ParseLocation.h"
#include "ParserClient.h"
#include "ReferenceKind.h"
#include "types.h"
#endif

inline void CxxDestructorCallRecorder::record(const clang::FunctionDecl* functionDecl)
{
	auto recordDestructorCall =
		[this](const clang::FunctionDecl* functionDecl, const clang::CXXDestructorDecl* destructorDecl)
	{
		Id referencedSymbolId = m_symbols.getOrCreateSymbolId(destructorDecl);
		Id contextSymbolId = m_symbols.getOrCreateSymbolId(m_context.getContext());
		// functionDecl->getEndLoc: the closing brace, where the implicit destructors run.
		ParseLocation parseLocation = m_locations.getParseLocation(functionDecl->getEndLoc());
		m_client.recordReference(ReferenceKind::CALL, referencedSymbolId, contextSymbolId, parseLocation);
	};

	// Adapted from:
	// "How to get information about call to destructors in Clang LibTooling?"
	// https://stackoverflow.com/questions/59610156/how-to-get-information-about-call-to-destructors-in-clang-libtooling

	if (functionDecl->isThisDeclarationADefinition())
	{
		clang::CFG::BuildOptions buildOptions;
		buildOptions.AddImplicitDtors = true;
		buildOptions.AddTemporaryDtors = true;

		if (std::unique_ptr<clang::CFG> cfg = clang::CFG::buildCFG(
				functionDecl, functionDecl->getBody(), &m_astContext, buildOptions))
		{
			for (clang::CFGBlock* block : cfg->const_nodes())
			{
				for (auto ref : block->refs())
				{
					// It should not be necessary to special-case 'CFGBaseDtor'. But
					// 'CFGImplicitDtor::getDestructorDecl' is simply missing the implementation of
					// that case. See 'CFGImplicitDtor::getDestructorDecl()':
					// https://github.com/llvm/llvm-project/blob/a0b8d548fd250c92c8f9274b57e38ad3f0b215e9/clang/lib/Analysis/CFG.cpp#L5465
					if (std::optional<clang::CFGBaseDtor> baseDtor = ref->getAs<clang::CFGBaseDtor>())
					{
						const clang::CXXBaseSpecifier* baseSpec = baseDtor->getBaseSpecifier();
						if (const clang::RecordType* recordType = clang::dyn_cast<clang::RecordType>(
								baseSpec->getType().getDesugaredType(m_astContext).getTypePtr()))
						{
							if (const clang::CXXRecordDecl* recordDecl =
									clang::dyn_cast<clang::CXXRecordDecl>(recordType->getDecl()))
							{
								if (const clang::CXXDestructorDecl* dtorDecl = recordDecl->getDestructor())
								{
									recordDestructorCall(functionDecl, dtorDecl);
								}
							}
						}
					}
					// If it were not for the above unimplemented functionality, we would only need
					// this block.
					else if (std::optional<clang::CFGImplicitDtor> implicitDtor =
								 ref->getAs<clang::CFGImplicitDtor>())
					{
						if (const clang::CXXDestructorDecl* dtorDecl =
								implicitDtor->getDestructorDecl(m_astContext))
						{
							recordDestructorCall(functionDecl, dtorDecl);
						}
					}
				}
			}
		}
	}
}

inline CxxDestructorCallRecorder::CxxDestructorCallRecorder(
	clang::ASTContext& astContext,
	ParserClient& client,
	CxxSymbolRegistry& symbols,
	CxxLocationExtractor& locations,
	CxxAstVisitorComponentContext& context)
	: m_astContext(astContext)
	, m_client(client)
	, m_symbols(symbols)
	, m_locations(locations)
	, m_context(context)
{
}
