#include "CxxAstVisitorComponentReferenceIndexer.h"

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

using namespace std;
using namespace clang;

CxxAstVisitorComponentReferenceIndexer::CxxAstVisitorComponentReferenceIndexer(
	CxxAstVisitor* astVisitor, clang::ASTContext* astContext, CxxIndexingContext& index)
	: CxxAstVisitorComponent(astVisitor)
	, m_astContext(astContext)
	, m_index(index)
{
}

void CxxAstVisitorComponentReferenceIndexer::wire()
{
	m_typeRefKind = getAstVisitor()->getTypeRefKindComponent();
	m_declRefKind = getAstVisitor()->getDeclRefKindComponent();
}

void CxxAstVisitorComponentReferenceIndexer::visitCastExpr(clang::CastExpr *d)
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

void CxxAstVisitorComponentReferenceIndexer::visitCXXFunctionalCastExpr(clang::CXXFunctionalCastExpr *d)
{
	if (getAstVisitor()->shouldVisitStmt(d))
	{
		if (QualType qualType = d->getType(); !qualType.isNull())
		{
			const Id contextSymbolId = m_index.getOrCreateSymbolId(m_index.getContext());
			m_index.recordDeducedQualType(qualType, contextSymbolId, m_index.getParseLocation(d->getBeginLoc()));
		}
	}
}

void CxxAstVisitorComponentReferenceIndexer::visitConceptSpecializationExpr(clang::ConceptSpecializationExpr *d)
{
	if (getAstVisitor()->shouldVisitStmt(d))
	{
		m_index.concepts().recordConceptReference(d);
	}
}

void CxxAstVisitorComponentReferenceIndexer::visitConceptReference(clang::ConceptReference *d)
{
	if (getAstVisitor()->shouldVisitReference(d->getLocation()))
	{
		m_index.concepts().recordNamedConceptReference(d);
	}
}

void CxxAstVisitorComponentReferenceIndexer::visitDeclRefExpr(clang::DeclRefExpr* s)
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
				m_index.recordSymbolKind(symbolId, SymbolKind::FUNCTION);
			}
			const Id contextSymbolId = m_index.getOrCreateSymbolId(m_index.getContext());
			m_index.recordReference(refKind, symbolId, contextSymbolId, m_index.getParseLocation(s->getLocation()));
		}
	}
}

void CxxAstVisitorComponentReferenceIndexer::visitMemberExpr(clang::MemberExpr* s)
{
	if (getAstVisitor()->shouldVisitReference(s->getMemberLoc()))
	{
		const Id symbolId = m_index.getOrCreateSymbolId(s->getMemberDecl());
		const Id contextSymbolId = m_index.getOrCreateSymbolId(m_index.getContext());
		const ReferenceKind refKind = consumeDeclRefContextKind();

		if (refKind == ReferenceKind::CALL)
		{
			m_index.recordSymbolKind(symbolId, SymbolKind::FUNCTION);
		}
		m_index.recordReference(refKind, symbolId, contextSymbolId, m_index.getParseLocation(s->getMemberLoc()));
	}
}

void CxxAstVisitorComponentReferenceIndexer::visitCXXConstructExpr(clang::CXXConstructExpr* s)
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

		const ReferenceKind refKind = consumeDeclRefContextKind();
		if (refKind == ReferenceKind::CALL)
		{
			m_index.recordSymbolKind(symbolId, SymbolKind::FUNCTION);
		}

		m_index.recordReference(
			refKind,
			symbolId,
			m_index.getOrCreateSymbolId(
				m_index.getContext()),
			m_index.getParseLocation(loc));
	}
}

void CxxAstVisitorComponentReferenceIndexer::visitCXXDeleteExpr(clang::CXXDeleteExpr* s)
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

					m_index.recordReference(
						ReferenceKind::CALL,
						symbolId,
						m_index.getOrCreateSymbolId(
							m_index.getContext()),
						m_index.getParseLocation(s->getBeginLoc()));
				}
			}
		}
	}
}

void CxxAstVisitorComponentReferenceIndexer::visitLambdaExpr(clang::LambdaExpr* s)
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

		if (const CXXRecordDecl *closureRecordDecl = s->getLambdaClass())
		{
			for (Decl *decl : closureRecordDecl->decls())
			{
				if (const FunctionTemplateDecl *functionTemplateDecl = dyn_cast<FunctionTemplateDecl>(decl))
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

void CxxAstVisitorComponentReferenceIndexer::visitConstructorInitializer(clang::CXXCtorInitializer* init)
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

ReferenceKind CxxAstVisitorComponentReferenceIndexer::consumeDeclRefContextKind()
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
