// Inline implementations for CxxAstVisitorComponentReferenceIndexer.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitor.h"
#include "CxxAstVisitorComponentDeclRefKind.h"
#include "CxxAstVisitorComponentTypeRefKind.h"
#include "CxxIndexingContext.h"
#include "clang_compat/ClangCompat.h"
#include "utilityClang.h"
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Preprocessor.h>
#endif

inline CxxAstVisitorComponentReferenceIndexer::CxxAstVisitorComponentReferenceIndexer(
	CxxAstVisitor* astVisitor, clang::ASTContext* astContext, CxxIndexingContext& index)
	: CxxAstVisitorComponent(astVisitor)
	, m_astContext(astContext)
	, m_index(index)
{
}

inline void CxxAstVisitorComponentReferenceIndexer::wire()
{
	m_typeRefKind = getAstVisitor()->getTypeRefKindComponent();
	m_declRefKind = getAstVisitor()->getDeclRefKindComponent();
}

inline void CxxAstVisitorComponentReferenceIndexer::visitCastExpr(clang::CastExpr *d)
{
	if (getAstVisitor()->shouldVisitStmt(d))
	{
		if (d->getCastKind() == clang::CK_UserDefinedConversion)
		{
			const Id referencedSymbolId = m_index.getOrCreateSymbolId(d->getConversionFunction());
			const Id contextSymbolId = m_index.getOrCreateSymbolId(m_index.getContext());
			const ParseLocation location = m_index.getParseLocation(d->getSourceRange());

			m_index.recordReference(ReferenceKind::CALL, referencedSymbolId, contextSymbolId, location);
		}
	}
}

inline void CxxAstVisitorComponentReferenceIndexer::visitCXXFunctionalCastExpr(clang::CXXFunctionalCastExpr *d)
{
	if (getAstVisitor()->shouldVisitStmt(d))
	{
		if (clang::QualType qualType = d->getType(); !qualType.isNull())
		{
			const Id contextSymbolId = m_index.getOrCreateSymbolId(m_index.getContext());
			m_index.recordDeducedQualType(qualType, contextSymbolId, m_index.getParseLocation(d->getBeginLoc()));
		}
	}
}

inline void CxxAstVisitorComponentReferenceIndexer::visitConceptSpecializationExpr(clang::ConceptSpecializationExpr *d)
{
	if (getAstVisitor()->shouldVisitStmt(d))
	{
		m_index.concepts().recordConceptReference(d);
		m_index.concepts().recordSatisfiedConceptSpecialization(d);
	}
}

inline void CxxAstVisitorComponentReferenceIndexer::visitConceptReference(clang::ConceptReference *d)
{
	if (getAstVisitor()->shouldVisitReference(d->getLocation()))
	{
		m_index.concepts().recordNamedConceptReference(d);
	}
}

inline void CxxAstVisitorComponentReferenceIndexer::visitDeclRefExpr(clang::DeclRefExpr* s)
{
	const clang::ValueDecl *decl = s->getDecl();

	// If we don't check for anonymous declarations here, then structured binding variable locations are not recorded separately!
	if (getAstVisitor()->shouldVisitReference(s->getLocation()) && !decl->getNameAsString().empty())
	{
		// Check for function parameter:
		if (clang::isa<clang::ParmVarDecl>(decl) || (clang::isa<clang::VarDecl>(decl) && decl->getParentFunctionOrMethod() != nullptr))
		{
			if (!utility::isImplicit(decl))
			{
				m_index.recordLocalSymbol(m_index.getLocalSymbolName(decl->getLocation()), m_index.getParseLocation(s->getLocation()));
			}
		}
		// Check for template parameter:
		else if (clang::isa<clang::NonTypeTemplateParmDecl>(decl) || clang::isa<clang::TemplateTypeParmDecl>(decl) || clang::isa<clang::TemplateTemplateParmDecl>(decl))
		{
			m_index.recordLocalSymbol(m_index.getLocalSymbolName(decl->getLocation()), m_index.getParseLocation(s->getLocation()));
		}
		// Check for structured binding declaration:
		else if (clang::isa<clang::BindingDecl>(decl))
		{
			// Without this line the structured binding variables are not indexed as local variables!
			m_index.recordLocalSymbol(m_index.getLocalSymbolName(decl->getLocation()), m_index.getParseLocation(s->getLocation()));
		}
		else
		{
			Id symbolId = m_index.getOrCreateSymbolId(decl);

			const ReferenceKind refKind = consumeDeclRefContextKind();
			if (refKind == ReferenceKind::CALL)
			{
				m_index.recordSymbolKind(symbolId, utility::getFunctionKind(decl));
			}
			const Id contextSymbolId = m_index.getOrCreateSymbolId(m_index.getContext());
			const Id edgeId = m_index.recordReference(
				refKind, symbolId, contextSymbolId, m_index.getParseLocation(s->getLocation()));

			// A specialization of a constrained function or variable template: its arguments
			// satisfy the template's concepts.
			if (const auto* function = clang::dyn_cast<clang::FunctionDecl>(decl))
			{
				m_index.concepts().recordSatisfiedConstraints(function, s->getLocation());
			}
			else if (const auto* variable = clang::dyn_cast<clang::VarTemplateSpecializationDecl>(decl))
			{
				m_index.concepts().recordSatisfiedConstraints(variable, s->getLocation());
			}

			// A member operator called with operator syntax (*a == *b): its object is the
			// call's first argument.
			if (refKind == ReferenceKind::CALL && !m_operatorCalls.empty())
			{
				const clang::CXXOperatorCallExpr* call = m_operatorCalls.back();
				if (call->getCallee()->IgnoreImplicit() == s && call->getNumArgs() > 0)
				{
					m_index.recordCallDispatch(
						edgeId,
						utility::callDispatches(
							clang::dyn_cast<clang::CXXMethodDecl>(decl),
							call->getArg(0),
							false,
							m_astContext->getLangOpts(),
							contextDecl()));
				}
			}
		}
	}
}

inline void CxxAstVisitorComponentReferenceIndexer::visitMemberExpr(clang::MemberExpr* s)
{
	if (getAstVisitor()->shouldVisitReference(s->getMemberLoc()))
	{
		const ReferenceKind refKind = consumeDeclRefContextKind();

		if (refKind == ReferenceKind::CALL)
		{
			m_index.recordSymbolKind(
				m_index.getOrCreateSymbolId(s->getMemberDecl()), utility::getFunctionKind(s->getMemberDecl()));
		}
		const Id edgeId = m_index.recordReference(s->getMemberDecl(), refKind, s->getMemberLoc());

		// a specialization of a constrained member function template
		if (const auto* function = clang::dyn_cast<clang::FunctionDecl>(s->getMemberDecl()))
		{
			m_index.concepts().recordSatisfiedConstraints(function, s->getMemberLoc());
		}

		// p->f(), r.f(), Base::f(), o.f(): whether the call dispatches is a property of the
		// member expression, its qualifier and its object.
		if (refKind == ReferenceKind::CALL)
		{
			m_index.recordCallDispatch(
				edgeId,
				utility::callDispatches(
					clang::dyn_cast<clang::CXXMethodDecl>(s->getMemberDecl()),
					s->getBase(),
					!s->performsVirtualDispatch(m_astContext->getLangOpts()),
					m_astContext->getLangOpts(),
					contextDecl()));
		}
	}
}

// A field designator (".a = x", each of ".a.b = x") is a use of its field at the field's name: how
// a C struct of function pointers is filled. An array designator ("[2] = x") names no field; its
// index is an expression of its own.
inline void CxxAstVisitorComponentReferenceIndexer::visitDesignatedInitExpr(clang::DesignatedInitExpr* s)
{
	for (const clang::DesignatedInitExpr::Designator& designator: s->designators())
	{
		if (!designator.isFieldDesignator())
		{
			continue;
		}

		// A designator of a member of an anonymous struct or union is preceded by one of that
		// struct or union, at the same location: that one is not written.
		const clang::FieldDecl* fieldDecl = designator.getFieldDecl();
		if (fieldDecl == nullptr || fieldDecl->isAnonymousStructOrUnion())
		{
			continue;
		}

		if (getAstVisitor()->shouldVisitReference(designator.getFieldLoc()))
		{
			m_index.recordReference(fieldDecl, ReferenceKind::USAGE, designator.getFieldLoc());
		}
	}
}

inline void CxxAstVisitorComponentReferenceIndexer::visitCXXConstructExpr(clang::CXXConstructExpr* s)
{
	const clang::CXXConstructorDecl* constructorDecl = s->getConstructor();

	if (!constructorDecl)
	{
		return;
	}
	else
	{
		const clang::CXXRecordDecl* parentDecl = constructorDecl->getParent();
		if (!parentDecl || parentDecl->isLambda())
		{
			return;
		}
	}

	if (getAstVisitor()->shouldVisitReference(s->getLocation()))
	{
		clang::SourceLocation loc;
		clang::SourceLocation braceBeginLoc = s->getParenOrBraceRange().getBegin();
		clang::SourceLocation nameBeginLoc = s->getSourceRange().getBegin();
		if (braceBeginLoc.isValid())
		{
			if (braceBeginLoc == nameBeginLoc)
			{
				loc = nameBeginLoc;
			}
			else
			{
				loc = braceBeginLoc.getLocWithOffset(-1);
			}
		}
		else
		{
			loc = s->getSourceRange().getEnd();
		}
		loc = clang::Lexer::GetBeginningOfToken(
			loc, m_astContext->getSourceManager(), m_astContext->getLangOpts());

		const Id symbolId = m_index.getOrCreateSymbolId(s->getConstructor());

		// Box b{circle} (the class template's arguments deduced, so no type names them) and a
		// constrained constructor template: the specializations' arguments satisfy their concepts.
		m_index.concepts().recordSatisfiedConstraints(
			clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(constructorDecl->getParent()), loc);
		m_index.concepts().recordSatisfiedConstraints(s->getConstructor(), loc);

		const ReferenceKind refKind = consumeDeclRefContextKind();
		if (refKind == ReferenceKind::CALL)
		{
			m_index.recordSymbolKind(symbolId, SymbolKind::METHOD);
		}

		m_index.recordReference(
			refKind,
			symbolId,
			m_index.getOrCreateSymbolId(
				m_index.getContext()),
			m_index.getParseLocation(loc));
	}
}

inline void CxxAstVisitorComponentReferenceIndexer::visitCXXDeleteExpr(clang::CXXDeleteExpr* s)
{
	if (!s->isArrayForm() && getAstVisitor()->shouldVisitReference(s->getBeginLoc()))
	{
		clang::QualType destroyedTypeQual = s->getDestroyedType();
		const clang::Type* destroyedType = destroyedTypeQual.getTypePtrOrNull();
		if (destroyedType != nullptr)
		{
			clang::CXXRecordDecl* recordDecl = destroyedType->getAsCXXRecordDecl();
			if (recordDecl != nullptr)
			{
				clang::CXXDestructorDecl* destructorDecl = recordDecl->getDestructor();
				if (destructorDecl != nullptr)
				{
					const Id symbolId = m_index.getOrCreateSymbolId(destructorDecl);

					const Id edgeId = m_index.recordReference(
						ReferenceKind::CALL,
						symbolId,
						m_index.getOrCreateSymbolId(
							m_index.getContext()),
						m_index.getParseLocation(s->getBeginLoc()));

					// delete p runs the destructor of the object's dynamic type when the
					// destructor is virtual, as a virtual call through p would (EmitObjectDelete).
					m_index.recordCallDispatch(
						edgeId,
						utility::callDispatches(
							destructorDecl, s->getArgument(), false, m_astContext->getLangOpts(), contextDecl()));
				}
			}
		}
	}
}

inline void CxxAstVisitorComponentReferenceIndexer::visitLambdaExpr(clang::LambdaExpr* s)
{
	if (getAstVisitor()->shouldVisitStmt(s))
	{
		if (const clang::CXXMethodDecl* methodDecl = s->getCallOperator())
		{
			Id symbolId = m_index.getOrCreateSymbolId(methodDecl);
			m_index.recordSymbolKind(symbolId, SymbolKind::FUNCTION);
			m_index.recordLocation(symbolId, m_index.getParseLocation(s->getBeginLoc()), ParseLocationType::TOKEN);
			m_index.recordLocation(symbolId, m_index.getParseLocationOfFunctionBody(methodDecl), ParseLocationType::SCOPE);
			m_index.recordDefinitionKind(symbolId, utility::getDefinitionKind(methodDecl));
		}

		// Iterate over the "closure type parameters" to detect concept usages:

		if (const clang::CXXRecordDecl *closureRecordDecl = s->getLambdaClass())
		{
			for (clang::Decl *decl : closureRecordDecl->decls())
			{
				if (const clang::FunctionTemplateDecl *functionTemplateDecl = dyn_cast<clang::FunctionTemplateDecl>(decl))
				{
					if (functionTemplateDecl->getTemplatedDecl() != nullptr)
					{
						m_index.concepts().recordTemplateParameterConceptReferences(functionTemplateDecl);
					}
				}
			}
		}
	}
}

inline void CxxAstVisitorComponentReferenceIndexer::visitConstructorInitializer(clang::CXXCtorInitializer* init)
{
	if (getAstVisitor()->shouldVisitReference(init->getMemberLocation()))
	{
		// record the field usage here because it is not a DeclRefExpr
		if (clang::FieldDecl* memberDecl = init->getMember())
		{
			m_index.recordReference(
				ReferenceKind::USAGE,
				m_index.getOrCreateSymbolId(memberDecl),
				m_index.getOrCreateSymbolId(
					m_index.getContext()),
				m_index.getParseLocation(init->getMemberLocation()));
		}
	}
}

inline void CxxAstVisitorComponentReferenceIndexer::beginTraverseCXXOperatorCall(clang::CXXOperatorCallExpr* s)
{
	m_operatorCalls.push_back(s);
}

inline void CxxAstVisitorComponentReferenceIndexer::endTraverseCXXOperatorCall(clang::CXXOperatorCallExpr*  /*s*/)
{
	m_operatorCalls.pop_back();
}

inline const clang::Decl* CxxAstVisitorComponentReferenceIndexer::contextDecl() const
{
	const CxxContext context = m_index.getContext();
	if (!context)
	{
		return nullptr;
	}
	return clang::dyn_cast<const clang::NamedDecl*>(context);
}

inline ReferenceKind CxxAstVisitorComponentReferenceIndexer::consumeDeclRefContextKind()
{
	if (m_typeRefKind->isTraversingInheritance())
	{
		return ReferenceKind::INHERITANCE;
	}
	else if (m_typeRefKind->isTraversingTemplateArgument())
	{
		return ReferenceKind::TYPE_USAGE;
	}

	return m_declRefKind->getReferenceKind();
}
