// Inline implementations for CxxAstVisitor.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <sstream>
#include <clang/AST/ASTContext.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Lex/Preprocessor.h>
#include "CanonicalFilePathCache.h"
#include "CxxDeclNameResolver.h"
#include "IndexerStateInfo.h"
#include "ParseLocation.h"
#include "ParserClient.h"
#include "ScopedSwitcher.h"
#include "clang_compat/ClangCompat.h"
#include "logging.h"
#include "utilityClang.h"
#endif

inline CxxAstVisitor::CxxAstVisitor(
	clang::ASTContext* astContext,
	clang::Preprocessor* preprocessor,
	ParserClient& client,
	CanonicalFilePathCache& canonicalFilePathCache,
	std::shared_ptr<IndexerStateInfo> indexerStateInfo,
	bool isVerbose)
	: m_astContext(astContext)
	, m_preprocessor(preprocessor)
	, m_client(client)
	, m_indexerStateInfo(indexerStateInfo)
	, m_canonicalFilePathCache(canonicalFilePathCache)
	, m_isVerbose(isVerbose)
	, m_locations(*astContext, preprocessor, canonicalFilePathCache)
	, m_index(*astContext, client, canonicalFilePathCache, m_locations)
	, m_components(
		  CxxAstVisitorComponentContext(this),
		  CxxAstVisitorComponentTypeRefKind(this),
		  CxxAstVisitorComponentDeclRefKind(this),
		  CxxAstVisitorComponentImplicitCode(this),
		  CxxAstVisitorComponentDeclarationIndexer(this, m_index),
		  CxxAstVisitorComponentReferenceIndexer(this, astContext, m_index),
		  CxxAstVisitorComponentTypeIndexer(this, m_index),
		  CxxAstVisitorComponentModuleIndexer(this, astContext, m_index),
		  CxxAstVisitorComponentBraceRecorder(this, astContext, client))
{
	// The context component now exists; hand it to the indexing facade (it could not be reached
	// during m_index's own construction, as the tuple was not yet built).
	m_index.setContext(std::get<CxxAstVisitorComponentContext>(m_components));

	// All components now exist; let each cache pointers to the siblings it depends on.
	forEachComponent([](auto& component) { component.wire(); });
}

inline CxxAstVisitorComponentContext *CxxAstVisitor::getContextComponent()
{
	return &std::get<CxxAstVisitorComponentContext>(m_components);
}

inline CxxAstVisitorComponentTypeRefKind *CxxAstVisitor::getTypeRefKindComponent()
{
	return &std::get<CxxAstVisitorComponentTypeRefKind>(m_components);
}

inline CxxAstVisitorComponentDeclRefKind *CxxAstVisitor::getDeclRefKindComponent()
{
	return &std::get<CxxAstVisitorComponentDeclRefKind>(m_components);
}

inline CanonicalFilePathCache* CxxAstVisitor::getCanonicalFilePathCache() const
{
	return &m_canonicalFilePathCache;
}

inline CxxLocationExtractor& CxxAstVisitor::getLocationExtractor()
{
	return m_locations;
}

inline void CxxAstVisitor::indexDecl(clang::Decl* d)
{
	LOG_INFO("starting AST traversal");
	this->TraverseDecl(d);
}

inline bool CxxAstVisitor::shouldVisitTemplateInstantiations() const
{
	return true;
}

inline bool CxxAstVisitor::shouldVisitImplicitCode() const
{
	return std::get<CxxAstVisitorComponentImplicitCode>(m_components).shouldVisitImplicitCode();
}

inline bool CxxAstVisitor::shouldHandleTypeLoc(const clang::TypeLoc& tl)
{
	return tl.getAs<clang::TagTypeLoc>() ||
		tl.getAs<clang::TypedefTypeLoc>() ||
		tl.getAs<clang::TemplateTypeParmTypeLoc>() ||
		tl.getAs<clang::TemplateSpecializationTypeLoc>() ||
		tl.getAs<clang::InjectedClassNameTypeLoc>() ||
		tl.getAs<clang::DependentNameTypeLoc>() ||
		tl.getAs<clang::SubstTemplateTypeParmTypeLoc>() ||
		tl.getAs<clang::BuiltinTypeLoc>()
#if LLVM_VERSION_MAJOR < 22
		|| tl.getAs<clang::DependentTemplateSpecializationTypeLoc>()
#endif
		;
}

inline bool CxxAstVisitor::TraverseDecl(clang::Decl* decl)
{
	if (m_isVerbose)
	{
		logVerboseDecl(decl);
	}
	const ScopedSwitcher<unsigned int> indentation(m_indentation, m_indentation + 1);

	bool traverse = true;
	if (decl)
	{
		const clang::SourceManager& sourceManager = m_astContext->getSourceManager();
		clang::SourceLocation loc = sourceManager.getExpansionLoc(decl->getLocation());

		if (loc.isInvalid())
		{
			loc = decl->getLocation();
		}

		if (loc.isValid())
		{
			// record files not handled in preprocessor callbacks, e.g. files within precompiled header
			const clang::FileID fileId = sourceManager.getFileID(loc);
			if (fileId.isValid() && m_canonicalFilePathCache.getFileSymbolId(fileId) == 0)
			{
				const FilePath filePath = m_canonicalFilePathCache.getCanonicalFilePath(
					fileId, sourceManager);
				const bool pathIsProjectFile = m_canonicalFilePathCache.isProjectFile(
					fileId, sourceManager);
				const Id symbolId = m_client.recordFile(filePath, pathIsProjectFile);
				m_client.recordFileLanguage(symbolId, "cpp");
				m_canonicalFilePathCache.addFileSymbolId(fileId, filePath, symbolId);
			}

			traverse = isLocatedInProjectFile(loc);
		}
	}

	if (traverse)
	{
		forEachComponent([&](auto& component) { component.beginTraverseDecl(decl); });
		Base::TraverseDecl(decl);
		forEachComponent([&](auto& component) { component.endTraverseDecl(decl); });
	}

	if (m_indexerStateInfo && m_indexerStateInfo->indexingInterrupted)
	{
		LOG_INFO("interrupting AST traversal");
		return false;
	}
	return true;
}

// same as Base::TraverseQualifiedTypeLoc(..) but we need to make sure to call this.TraverseTypeLoc(..)
inline bool CxxAstVisitor::TraverseQualifiedTypeLoc(
	clang::QualifiedTypeLoc tl, bool TraverseQualifier)
{
	return TraverseTypeLoc(tl.getUnqualifiedLoc(), TraverseQualifier);
}

inline bool CxxAstVisitor::TraverseTypeLoc(clang::TypeLoc v, bool TraverseQualifier)
{
	if (m_isVerbose)
	{
		logVerboseTypeLoc(v);
	}
	const ScopedSwitcher<unsigned int> indentation(m_indentation, m_indentation + 1);

	forEachComponent([&](auto& component) { component.beginTraverseTypeLoc(v); });
	clang_compat::traverseTypeLoc(static_cast<Base&>(*this), v, TraverseQualifier);
	forEachComponent([&](auto& component) { component.endTraverseTypeLoc(v); });
	return true;
}

inline bool CxxAstVisitor::TraverseType(clang::QualType v, bool TraverseQualifier)
{
	forEachComponent([&](auto& component) { component.beginTraverseType(v); });
	clang_compat::traverseType(static_cast<Base&>(*this), v, TraverseQualifier);
	forEachComponent([&](auto& component) { component.endTraverseType(v); });
	return true;
}

inline bool CxxAstVisitor::TraverseStmt(clang::Stmt* v)
{
	if (m_isVerbose)
	{
		logVerboseStmt(v);
	}
	const ScopedSwitcher<unsigned int> indentation(m_indentation, m_indentation + 1);

	return traverseWithComponents(
		[&](auto& component) { component.beginTraverseStmt(v); },
		[&] { Base::TraverseStmt(v); },
		[&](auto& component) { component.endTraverseStmt(v); });
}

inline std::string CxxAstVisitor::getIndentString() const
{
	std::string indentString;
	for (unsigned int i = 0; i < m_indentation; i++)
	{
		indentString += "| ";
	}
	return indentString;
}

// ODR-safe home for the verbose-logging helpers: anonymous namespaces are an ODR trap in
// headers/inls (each includer gets a distinct entity referenced from inline bodies).
namespace cxx_ast_visitor_detail
{
inline std::string obfuscateName(const std::string& name)
{
	if (name.length() <= 2)
	{
		return name;
	}
	return name.substr(0, 1) + ".." + name.substr(name.length() - 1);
}

inline std::string typeLocClassToString(clang::TypeLoc tl)
{
	switch (tl.getTypeLocClass())
	{
#define STRINGIFY(X) #X
#define ABSTRACT_TYPE(Class, Base)
#define TYPE(Class, Base)                                                                          \
	case clang::TypeLoc::Class:                                                                     \
		return STRINGIFY(Class);
#include <clang/AST/TypeLoc.h>
#undef TYPE
#undef ABSTRACT_TYPE
#undef STRINGIFY
	case clang::TypeLoc::TypeLocClass::Qualified:
		return "Qualified";
	default:
		return "";
	}
}
}	 // namespace cxx_ast_visitor_detail

inline void CxxAstVisitor::logVerboseDecl(clang::Decl* d)
{
	if (!d)
	{
		return;
	}

	std::stringstream stream;
	stream << getIndentString() << d->getDeclKindName() << "Decl";
	if (clang::NamedDecl* namedDecl = clang::dyn_cast_or_null<clang::NamedDecl>(d))
	{
		stream << " [" << cxx_ast_visitor_detail::obfuscateName(namedDecl->getNameAsString()) << "]";
	}

	ParseLocation loc = m_locations.getParseLocation(d->getSourceRange());
	stream << " <" << loc.startLineNumber << ":" << loc.startColumnNumber << ", "
		   << loc.endLineNumber << ":" << loc.endColumnNumber << ">";

	const clang::SourceManager& sm = m_astContext->getSourceManager();
	FilePath currentFilePath = getCanonicalFilePathCache()->getCanonicalFilePath(
		sm.getFileID(d->getSourceRange().getBegin()), sm);
	if (m_currentFilePath != currentFilePath)
	{
		m_currentFilePath = currentFilePath;
		LOG_INFO_BARE("Indexer - Traversing \"" + currentFilePath.str() + "\"");
	}

	LOG_INFO_STREAM_BARE(<< "Indexer - " << stream.str());
}

inline void CxxAstVisitor::logVerboseStmt(clang::Stmt* stmt)
{
	if (!stmt)
	{
		return;
	}

	ParseLocation loc = m_locations.getParseLocation(stmt->getSourceRange());
	LOG_INFO_STREAM_BARE(
		<< "Indexer - " << getIndentString() << stmt->getStmtClassName() << " <"
		<< loc.startLineNumber << ":" << loc.startColumnNumber << ", " << loc.endLineNumber << ":"
		<< loc.endColumnNumber << ">");
}

inline void CxxAstVisitor::logVerboseTypeLoc(clang::TypeLoc tl)
{
	if (tl.isNull())
	{
		return;
	}

	ParseLocation loc = m_locations.getParseLocation(tl.getSourceRange());
	LOG_INFO_STREAM_BARE(
		<< "Indexer - " << getIndentString() << cxx_ast_visitor_detail::typeLocClassToString(tl) << "TypeLoc <"
		<< loc.startLineNumber << ":" << loc.startColumnNumber << ", " << loc.endLineNumber << ":"
		<< loc.endColumnNumber << ">");
}

// same as Base::TraverseCXXRecordDecl(..) but we need to integrate the setter for the context info.
// additionally: skip implicit CXXRecordDecls (this does not skip template specializations).
inline bool CxxAstVisitor::TraverseCXXRecordDecl(clang::CXXRecordDecl* d)
{
	if (utility::isImplicit(d) && d->getMemberSpecializationInfo() == nullptr &&
		!clang::isa<clang::ClassTemplateSpecializationDecl>(utility::getFirstDecl(d)))
	{
		return true;
	}

	if (d->isLambda())
	{
		return TraverseFunctionDecl(d->getLambdaCallOperator());
	}

	WalkUpFromCXXRecordDecl(d);

	TraverseNestedNameSpecifierLoc(d->getQualifierLoc());

	if (d->hasDefinition())
	{
		for (const auto& base: d->bases())
		{
			if (!traverseCXXBaseSpecifier(base))
			{
				return false;
			}
		}
	}

	traverseDeclContextHelper(clang::dyn_cast<clang::DeclContext>(d));
	return true;
}

inline bool CxxAstVisitor::traverseCXXBaseSpecifier(const clang::CXXBaseSpecifier& d)
{
	forEachComponent([&](auto& component) { component.beginTraverseCXXBaseSpecifier(); });
	bool ret = TraverseTypeLoc(d.getTypeSourceInfo()->getTypeLoc());
	forEachComponent([&](auto& component) { component.endTraverseCXXBaseSpecifier(); });
	return ret;
}

inline bool CxxAstVisitor::TraverseCXXMethodDecl(clang::CXXMethodDecl* d)
{
	if (d->getTemplatedKind() == clang::CXXMethodDecl::TK_FunctionTemplate)
	{
		if (clang::CXXRecordDecl* recordDecl = d->getParent())
		{
			if (!clang::isa<clang::ClassTemplatePartialSpecializationDecl>(recordDecl) &&
				clang::isa<clang::ClassTemplateSpecializationDecl>(recordDecl) &&
				!clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(recordDecl)
					 ->isExplicitSpecialization())
			{
				return true;	// we skip visiting an implicit definition of a template method and
								// its contents
			}
		}
	}
	return Base::TraverseCXXMethodDecl(d);
}

// same as Base::TraverseTemplateTypeParmDecl(..) but we need to integrate the setter for the context info.
inline bool CxxAstVisitor::TraverseTemplateTypeParmDecl(clang::TemplateTypeParmDecl* d)
{
	WalkUpFromTemplateTypeParmDecl(d);

	if (d->hasDefaultArgument() && !d->defaultArgumentWasInherited())
	{
		forEachComponent([&](auto& component) { component.beginTraverseTemplateDefaultArgumentLoc(); });
#if LLVM_VERSION_MAJOR >= 19
		TraverseTypeLoc(d->getDefaultArgument().getTypeSourceInfo()->getTypeLoc());
#else
		TraverseTypeLoc(d->getDefaultArgumentInfo()->getTypeLoc());
#endif
		forEachComponent([&](auto& component) { component.endTraverseTemplateDefaultArgumentLoc(); });
	}

	traverseDeclContextHelper(clang::dyn_cast<clang::DeclContext>(d));
	return true;
}

// same as Base::TraverseTemplateTemplateParmDecl(..) but we need to integrate the setter for the
// context info.
inline bool CxxAstVisitor::TraverseTemplateTemplateParmDecl(clang::TemplateTemplateParmDecl* d)
{
	WalkUpFromTemplateTemplateParmDecl(d);

	TraverseDecl(d->getTemplatedDecl());

	if (d->hasDefaultArgument() && !d->defaultArgumentWasInherited())
	{
		forEachComponent([&](auto& component) { component.beginTraverseTemplateDefaultArgumentLoc(); });
		TraverseTemplateArgumentLoc(d->getDefaultArgument());
		forEachComponent([&](auto& component) { component.endTraverseTemplateDefaultArgumentLoc(); });
	}

	clang::TemplateParameterList* TPL = d->getTemplateParameters();
	if (TPL)
	{
		for (clang::TemplateParameterList::iterator I = TPL->begin(), E = TPL->end(); I != E; ++I)
		{
			TraverseDecl(*I);
		}
	}

	traverseDeclContextHelper(clang::dyn_cast<clang::DeclContext>(d));
	return true;
}

inline bool CxxAstVisitor::VisitTranslationUnitDecl(clang::TranslationUnitDecl* d)
{
	forEachComponent([&](auto& component) { component.visitTranslationUnitDecl(d); });
	return true;
}

inline bool CxxAstVisitor::TraverseNestedNameSpecifierLoc(clang::NestedNameSpecifierLoc loc)
{
	bool ret = true;
	if (loc)
	{
		forEachComponent([&](auto& component) { component.beginTraverseNestedNameSpecifierLoc(loc); });

		clang::NestedNameSpecifierLoc prefix;
		if (clang_compat::getNestedNameSpecifierLocPrefix(loc, &prefix))
		{
			ret = TraverseNestedNameSpecifierLoc(prefix);
		}

		forEachComponent([&](auto& component) { component.endTraverseNestedNameSpecifierLoc(loc); });
	}
	return ret;
}

inline bool CxxAstVisitor::TraverseConstructorInitializer(clang::CXXCtorInitializer* init)
{
	forEachComponent([&](auto& component) { component.beginTraverseConstructorInitializer(init); });

	bool ret = VisitConstructorInitializer(init);
	if (ret)
	{
		ret = Base::TraverseConstructorInitializer(init);
	}

	forEachComponent([&](auto& component) { component.endTraverseConstructorInitializer(init); });

	return ret;
}

inline bool CxxAstVisitor::TraverseCallExpr(clang::CallExpr* s)
{
	return TraverseCallCommon(s);
}

inline bool CxxAstVisitor::TraverseCXXMemberCallExpr(clang::CXXMemberCallExpr* s)
{
	return TraverseCallCommon(s);
}

inline bool CxxAstVisitor::TraverseCXXOperatorCallExpr(clang::CXXOperatorCallExpr* s)
{
	return TraverseCallCommon(s);
}

inline bool CxxAstVisitor::TraverseCXXConstructExpr(clang::CXXConstructExpr* s)
{
	forEachComponent([&](auto& component) { component.beginTraverseCallCommonCallee(); });
	WalkUpFromCXXConstructExpr(s);
	forEachComponent([&](auto& component) { component.endTraverseCallCommonCallee(); });

	for (unsigned int i = 0; i < s->getNumArgs(); ++i)
	{
		forEachComponent([&](auto& component) { component.beginTraverseCallCommonArgument(); });
		TraverseStmt(s->getArg(i));
		forEachComponent([&](auto& component) { component.endTraverseCallCommonArgument(); });
	}
	return true;
}

inline bool CxxAstVisitor::TraverseCXXTemporaryObjectExpr(clang::CXXTemporaryObjectExpr* v)
{
	return traverseWithComponents(
		[&](auto& component) { component.beginTraverseCXXTemporaryObjectExpr(v); },
		[&] { Base::TraverseCXXTemporaryObjectExpr(v); },
		[&](auto& component) { component.endTraverseCXXTemporaryObjectExpr(v); });
}

inline bool CxxAstVisitor::TraverseLambdaExpr(clang::LambdaExpr* v)
{
	return traverseWithComponents(
		[&](auto& component) { component.beginTraverseLambdaExpr(v); },
		[&] { Base::TraverseLambdaExpr(v); },
		[&](auto& component) { component.endTraverseLambdaExpr(v); });
}

/*
This code would also detect lambdas with concept usages, but it somehow breaks the detection/indexing
of captured variables!

inline bool CxxAstVisitor::TraverseLambdaExpr(clang::LambdaExpr *s)
{
	forEachComponent([&](auto& component) { component.beginTraverseLambdaExpr(s); });

	// Iterate/Walk/Traverse over the "closure type" to detect concept usages:

	if (const CXXRecordDecl *closureRecordDecl = s->getLambdaClass())
	{
		for (Decl *decl : closureRecordDecl->decls())
		{
			// This will eventually call 'visitFunctionTemplateDecl':

			TraverseDecl(decl);
		}
	}
	forEachComponent([&](auto& component) { component.endTraverseLambdaExpr(s); });

	return true;
}
*/

inline bool CxxAstVisitor::TraverseFunctionDecl(clang::FunctionDecl* v)
{
	return traverseWithComponents(
		[&](auto& component) { component.beginTraverseFunctionDecl(v); },
		[&] { Base::TraverseFunctionDecl(v); },
		[&](auto& component) { component.endTraverseFunctionDecl(v); });
}

// same as base::TraverseClassTemplateSpecializationDecl but without traversing the typeloc of the
// template specialitation itself
inline bool CxxAstVisitor::TraverseClassTemplateSpecializationDecl(clang::ClassTemplateSpecializationDecl* D)
{
	forEachComponent([&](auto& component) { component.beginTraverseClassTemplateSpecializationDecl(D); });

	bool ShouldVisitChildren = true;
	bool ReturnValue = true;
	if (ReturnValue && !shouldTraversePostOrder())
	{
		if (!WalkUpFromClassTemplateSpecializationDecl(D))
		{
			ReturnValue = false;
		}
	}

	if (ReturnValue)
	{
#if LLVM_VERSION_MAJOR >= 19
		if (const clang::ASTTemplateArgumentListInfo *TALI = D->getTemplateArgsAsWritten())
		{
			for (const clang::TemplateArgumentLoc &TAL : TALI->arguments())
			{
				if (!TraverseTemplateArgumentLoc(TAL))
				{
					ReturnValue = false;
				}
			}
		}
#else
		if (clang::TypeSourceInfo* TSI = D->getTypeAsWritten())
		{
			// clang::TypeLoc::TypeLocClass ccccc = TSI->getTypeLoc().getTypeLocClass();
			const clang::TemplateSpecializationTypeLoc tstl =
				TSI->getTypeLoc().getAs<clang::TemplateSpecializationTypeLoc>();
			if (!tstl.isNull())
			{
				for (unsigned I = 0, E = tstl.getNumArgs(); I != E; ++I)
				{
					if (!TraverseTemplateArgumentLoc(tstl.getArgLoc(I)))
					{
						ReturnValue = false;
					}
				}
			}
		}
#endif
	}

	if (ReturnValue)
	{
		if (!TraverseNestedNameSpecifierLoc(D->getQualifierLoc()))
		{
			ReturnValue = false;
		}
	}

	if (ReturnValue && ShouldVisitChildren)
	{
		traverseDeclContextHelper(clang::dyn_cast<clang::DeclContext>(D));
	}

	if (ReturnValue && shouldTraversePostOrder())
	{
		if (!WalkUpFromClassTemplateSpecializationDecl(D))
		{
			return false;
		}
	}

	forEachComponent([&](auto& component) { component.endTraverseClassTemplateSpecializationDecl(D); });

	return ReturnValue;
}

inline bool CxxAstVisitor::TraverseClassTemplatePartialSpecializationDecl(
	clang::ClassTemplatePartialSpecializationDecl* v)
{
	return traverseWithComponents(
		[&](auto& component) { component.beginTraverseClassTemplatePartialSpecializationDecl(v); },
		[&] { Base::TraverseClassTemplatePartialSpecializationDecl(v); },
		[&](auto& component) { component.endTraverseClassTemplatePartialSpecializationDecl(v); });
}

inline bool CxxAstVisitor::TraverseDeclRefExpr(clang::DeclRefExpr* v)
{
	return traverseWithComponents(
		[&](auto& component) { component.beginTraverseDeclRefExpr(v); },
		[&] { Base::TraverseDeclRefExpr(v); },
		[&](auto& component) { component.endTraverseDeclRefExpr(v); });
}

inline bool CxxAstVisitor::TraverseCXXForRangeStmt(clang::CXXForRangeStmt* v)
{
	return traverseWithComponents(
		[&](auto& component) { component.beginTraverseCXXForRangeStmt(v); },
		[&] { Base::TraverseCXXForRangeStmt(v); },
		[&](auto& component) { component.endTraverseCXXForRangeStmt(v); });
}

inline bool CxxAstVisitor::TraverseTemplateSpecializationTypeLoc(
	clang::TemplateSpecializationTypeLoc v, bool TraverseQualifier)
{
	return traverseWithComponents(
		[&](auto& component) { component.beginTraverseTemplateSpecializationTypeLoc(v); },
		[&] { Base::TraverseTemplateSpecializationTypeLoc(v, TraverseQualifier); },
		[&](auto& component) { component.endTraverseTemplateSpecializationTypeLoc(v); });
}

inline bool CxxAstVisitor::TraverseUnresolvedLookupExpr(clang::UnresolvedLookupExpr* v)
{
	return traverseWithComponents(
		[&](auto& component) { component.beginTraverseUnresolvedLookupExpr(v); },
		[&] { Base::TraverseUnresolvedLookupExpr(v); },
		[&](auto& component) { component.endTraverseUnresolvedLookupExpr(v); });
}

inline bool CxxAstVisitor::TraverseUnresolvedMemberExpr(clang::UnresolvedMemberExpr* v)
{
	return traverseWithComponents(
		[&](auto& component) { component.beginTraverseUnresolvedMemberExpr(v); },
		[&] { Base::TraverseUnresolvedMemberExpr(v); },
		[&](auto& component) { component.endTraverseUnresolvedMemberExpr(v); });
}

inline bool CxxAstVisitor::TraverseTemplateArgumentLoc(const clang::TemplateArgumentLoc& loc)
{
	forEachComponent([&](auto& component) { component.beginTraverseTemplateArgumentLoc(loc); });
	bool ret = Base::TraverseTemplateArgumentLoc(loc);
	forEachComponent([&](auto& component) { component.endTraverseTemplateArgumentLoc(loc); });
	return ret;
}

inline bool CxxAstVisitor::TraverseLambdaCapture(
	clang::LambdaExpr* lambdaExpr, const clang::LambdaCapture* capture, clang::Expr*  /*Init*/)
{
	forEachComponent([&](auto& component) { component.beginTraverseLambdaCapture(lambdaExpr, capture); });
	bool ret = true;
	if (lambdaExpr->isInitCapture(capture))
	{
		ret = TraverseDecl(capture->getCapturedVar());
	}
	forEachComponent([&](auto& component) { component.endTraverseLambdaCapture(lambdaExpr, capture); });
	return ret;
}

inline bool CxxAstVisitor::TraverseBinComma(clang::BinaryOperator* s)
{
	forEachComponent([&](auto& component) { component.beginTraverseBinCommaLhs(); });
	TraverseStmt(s->getLHS());
	forEachComponent([&](auto& component) { component.endTraverseBinCommaLhs(); });

	forEachComponent([&](auto& component) { component.beginTraverseBinCommaRhs(); });
	TraverseStmt(s->getRHS());
	forEachComponent([&](auto& component) { component.endTraverseBinCommaRhs(); });
	return true;
}

inline bool CxxAstVisitor::TraverseDeclarationNameInfo(clang::DeclarationNameInfo  /*NameInfo*/)
{
	// we don't visit any children here
	return true;
}

inline void CxxAstVisitor::traverseDeclContextHelper(clang::DeclContext* d)
{
	if (!d)
	{
		return;
	}

	for (auto* child: d->decls())
	{
		// BlockDecls and CapturedDecls are traversed through BlockExprs and
		// CapturedStmts respectively.
		if (!llvm::isa<clang::BlockDecl>(child) && !llvm::isa<clang::CapturedDecl>(child))
		{
			TraverseDecl(child);
		}
	}
}

inline bool CxxAstVisitor::TraverseCallCommon(clang::CallExpr* s)
{
	forEachComponent([&](auto& component) { component.beginTraverseCallCommonCallee(); });
	TraverseStmt(s->getCallee());
	forEachComponent([&](auto& component) { component.endTraverseCallCommonCallee(); });

	for (unsigned int i = 0; i < s->getNumArgs(); ++i)
	{
		forEachComponent([&](auto& component) { component.beginTraverseCallCommonArgument(); });
		TraverseStmt(s->getArg(i));
		forEachComponent([&](auto& component) { component.endTraverseCallCommonArgument(); });
	}
	return true;
}

inline bool CxxAstVisitor::TraverseAssignCommon(clang::BinaryOperator* s)
{
	forEachComponent([&](auto& component) { component.beginTraverseAssignCommonLhs(); });
	TraverseStmt(s->getLHS());
	forEachComponent([&](auto& component) { component.endTraverseAssignCommonLhs(); });

	forEachComponent([&](auto& component) { component.beginTraverseAssignCommonRhs(); });
	TraverseStmt(s->getRHS());
	forEachComponent([&](auto& component) { component.endTraverseAssignCommonRhs(); });
	return true;
}

// Each Visit* override just fans out to the components' matching visit* hook. This is a
// legitimate use of the preprocessor: it generates ~40 uniform member definitions whose names
// are dictated externally. Clang's RecursiveASTVisitor finds each override by a compile-time
// name lookup (getDerived().Visit##Type), and fires a hook at every level of the AST type
// hierarchy, so the per-type overrides cannot be collapsed into a few root visitors. C++ has no
// way to synthesize a member function with a computed name (templates parameterize types/values,
// not identifiers; `##` is the only tool), so a template/enum/TypeSwitch alternative would still
// have to spell all 40 type<->hook pairs by hand.
//
// TODO(C++26): replace this with reflection + token injection (P2996 + P3294) once the toolchain
// supports it — that is the first non-preprocessor mechanism able to generate these members
// programmatically. Until then, keep the macro.
#define DEF_VISIT_CUSTOM_TYPE_PTR(__NAME_TYPE__, __PARAM_TYPE__)                                   \
	bool CxxAstVisitor::Visit##__NAME_TYPE__(clang::__PARAM_TYPE__* v)                             \
	{                                                                                              \
		forEachComponent([&](auto& component) { component.visit##__NAME_TYPE__(v); });                                                \
		return true;                                                                               \
	}

#define DEF_VISIT_CUSTOM_TYPE(__NAME_TYPE__, __PARAM_TYPE__)                                       \
	bool CxxAstVisitor::Visit##__NAME_TYPE__(clang::__PARAM_TYPE__ v)                              \
	{                                                                                              \
		forEachComponent([&](auto& component) { component.visit##__NAME_TYPE__(v); });                                                \
		return true;                                                                               \
	}

#define DEF_VISIT_TYPE_PTR(__TYPE__) DEF_VISIT_CUSTOM_TYPE_PTR(__TYPE__, __TYPE__)

#define DEF_VISIT_TYPE(__TYPE__) DEF_VISIT_CUSTOM_TYPE(__TYPE__, __TYPE__)

DEF_VISIT_TYPE_PTR(CastExpr)
DEF_VISIT_TYPE_PTR(CXXFunctionalCastExpr)
DEF_VISIT_CUSTOM_TYPE_PTR(UnaryAddrOf, UnaryOperator)
DEF_VISIT_CUSTOM_TYPE_PTR(UnaryDeref, UnaryOperator)
DEF_VISIT_TYPE_PTR(DeclStmt)
DEF_VISIT_TYPE_PTR(ReturnStmt)
DEF_VISIT_TYPE_PTR(CompoundStmt)
DEF_VISIT_TYPE_PTR(InitListExpr)
DEF_VISIT_TYPE_PTR(ImportDecl)
DEF_VISIT_TYPE_PTR(ExportDecl)
DEF_VISIT_TYPE_PTR(TagDecl)
DEF_VISIT_TYPE_PTR(ClassTemplateDecl)
DEF_VISIT_TYPE_PTR(ClassTemplateSpecializationDecl)
DEF_VISIT_TYPE_PTR(FunctionDecl)
DEF_VISIT_TYPE_PTR(FunctionTemplateDecl)
DEF_VISIT_TYPE_PTR(CXXMethodDecl)
DEF_VISIT_TYPE_PTR(VarDecl)
DEF_VISIT_TYPE_PTR(DecompositionDecl)
DEF_VISIT_TYPE_PTR(VarTemplateSpecializationDecl)
DEF_VISIT_TYPE_PTR(FieldDecl)
DEF_VISIT_TYPE_PTR(TypedefDecl)
DEF_VISIT_TYPE_PTR(TypeAliasDecl)
DEF_VISIT_TYPE_PTR(NamespaceDecl)
DEF_VISIT_TYPE_PTR(NamespaceAliasDecl)
DEF_VISIT_TYPE_PTR(EnumConstantDecl)
DEF_VISIT_TYPE_PTR(UsingDirectiveDecl)
DEF_VISIT_TYPE_PTR(UsingDecl)
DEF_VISIT_TYPE_PTR(NonTypeTemplateParmDecl)
DEF_VISIT_TYPE_PTR(TemplateTypeParmDecl)
DEF_VISIT_TYPE_PTR(TemplateTemplateParmDecl)
DEF_VISIT_TYPE_PTR(ConceptDecl)
DEF_VISIT_TYPE_PTR(ConceptSpecializationExpr)
DEF_VISIT_TYPE_PTR(ConceptReference)
DEF_VISIT_TYPE(TypeLoc)
DEF_VISIT_TYPE_PTR(DeclRefExpr)
DEF_VISIT_TYPE_PTR(MemberExpr)
DEF_VISIT_TYPE_PTR(CXXDependentScopeMemberExpr)
DEF_VISIT_TYPE_PTR(CXXConstructExpr)
DEF_VISIT_TYPE_PTR(CXXDeleteExpr)
DEF_VISIT_TYPE_PTR(LambdaExpr)
DEF_VISIT_TYPE_PTR(MSAsmStmt)
DEF_VISIT_CUSTOM_TYPE_PTR(ConstructorInitializer, CXXCtorInitializer)

#undef DEF_VISIT_CUSTOM_TYPE_PTR
#undef DEF_VISIT_CUSTOM_TYPE
#undef DEF_VISIT_TYPE_PTR
#undef DEF_VISIT_TYPE


inline bool CxxAstVisitor::shouldVisitStmt(const clang::Stmt* stmt) const
{
	if (stmt != nullptr)
	{
		clang::SourceLocation loc = m_astContext->getSourceManager().getExpansionLoc(stmt->getBeginLoc());

		if (loc.isInvalid())
		{
			loc = stmt->getBeginLoc();
		}

		if (isLocatedInProjectFile(loc))
		{
			return true;
		}
	}
	return false;
}

inline bool CxxAstVisitor::shouldVisitDecl(const clang::Decl* decl) const
{
	if (decl != nullptr)
	{
		clang::SourceLocation loc = m_astContext->getSourceManager().getExpansionLoc(decl->getLocation());

		if (loc.isInvalid())
		{
			loc = decl->getLocation();
		}

		if (isLocatedInProjectFile(loc))
		{
			return true;
		}
	}
	return false;
}

inline bool CxxAstVisitor::shouldVisitReference(const clang::SourceLocation& referenceLocation) const
{
	clang::SourceLocation loc = m_astContext->getSourceManager().getExpansionLoc(referenceLocation);
	if (loc.isInvalid())
	{
		loc = referenceLocation;
	}

	if (isLocatedInProjectFile(loc))
	{
		return true;
	}

	return false;
}

inline bool CxxAstVisitor::isLocatedInProjectFile(clang::SourceLocation loc) const
{
	if (loc.isInvalid())
	{
		return false;
	}

	const clang::SourceManager& sourceManager = m_astContext->getSourceManager();
	return m_canonicalFilePathCache.isProjectFile(sourceManager.getFileID(loc), sourceManager);
}

