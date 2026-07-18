#include "CxxAstVisitorComponentIndexer.h"

#include "CanonicalFilePathCache.h"
#include "CxxAstVisitor.h"
#include "CxxAstVisitorComponentDeclRefKind.h"
#include "CxxAstVisitorComponentTypeRefKind.h"
#include "CxxIndexingContext.h"
#include "NameHierarchy.h"
#include "clang_compat/ClangCompat.h"
#include "utilityClang.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Preprocessor.h>

using namespace std;
using namespace clang;

CxxAstVisitorComponentIndexer::CxxAstVisitorComponentIndexer(
	CxxAstVisitor* astVisitor, clang::ASTContext* astContext, CxxIndexingContext& index)
	: CxxAstVisitorComponent(astVisitor)
	, m_astContext(astContext)
	, m_index(index)
{
}

void CxxAstVisitorComponentIndexer::wire()
{
	m_typeRefKind = getAstVisitor()->getTypeRefKindComponent();
	m_declRefKind = getAstVisitor()->getDeclRefKindComponent();
}

void CxxAstVisitorComponentIndexer::beginTraverseNestedNameSpecifierLoc(
	const clang::NestedNameSpecifierLoc& loc)
{
	if (!getAstVisitor()->shouldVisitReference(loc.getBeginLoc()))
	{
		return;
	}

	const auto nestedNameSpecifier = loc.getNestedNameSpecifier();
	switch (clang_compat::getNestedNameSpecifierKind(nestedNameSpecifier))
	{
	case clang_compat::NestedNameSpecifierKind::Null:
	case clang_compat::NestedNameSpecifierKind::Global:
	case clang_compat::NestedNameSpecifierKind::Identifier:
	case clang_compat::NestedNameSpecifierKind::MicrosoftSuper:
		break;

	case clang_compat::NestedNameSpecifierKind::Namespace:
	{
		const clang::NamedDecl* namespaceDecl =
			clang_compat::getNestedNameSpecifierNamespaceDecl(nestedNameSpecifier);
		if (!namespaceDecl)
			break;

		Id symbolId = m_index.getOrCreateSymbolId(namespaceDecl);
		m_index.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);
		m_index.recordLocation(
			symbolId, m_index.getParseLocation(clang_compat::getNestedNameSpecifierLocalNameLoc(loc)), ParseLocationType::QUALIFIER);

		if (const auto* namespaceAliasDecl =
				clang::dyn_cast<clang::NamespaceAliasDecl>(namespaceDecl))
		{
			symbolId = m_index.getOrCreateSymbolId(namespaceAliasDecl->getAliasedNamespace());
			m_index.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);
		}
	}
	break;

	case clang_compat::NestedNameSpecifierKind::Type:
		if (const clang::CXXRecordDecl* recordDecl =
				clang_compat::getNestedNameSpecifierRecordDecl(nestedNameSpecifier))
		{
			SymbolKind symbolKind = SymbolKind::UNDEFINED;
			if (recordDecl->isClass())
			{
				symbolKind = SymbolKind::CLASS;
			}
			else if (recordDecl->isStruct())
			{
				symbolKind = SymbolKind::STRUCT;
			}
			else if (recordDecl->isUnion())
			{
				symbolKind = SymbolKind::UNION;
			}

			if (symbolKind != SymbolKind::UNDEFINED)
			{
				const Id symbolId = m_index.getOrCreateSymbolId(recordDecl);
				m_index.recordSymbolKind(symbolId, symbolKind);
				m_index.recordLocation(
					symbolId, m_index.getParseLocation(clang_compat::getNestedNameSpecifierLocalNameLoc(loc)), ParseLocationType::QUALIFIER);
			}
		}
		else if (
			const clang::Type* type = clang_compat::getNestedNameSpecifierType(nestedNameSpecifier))
		{
			const ParseLocation parseLocation = m_index.getParseLocation(clang_compat::getNestedNameSpecifierLocalNameLoc(loc));

			if (const clang::TemplateTypeParmType* tpt =
					clang::dyn_cast_or_null<clang::TemplateTypeParmType>(type))
			{
				clang::TemplateTypeParmDecl* d = tpt->getDecl();
				if (d)
				{
					m_index.recordLocalSymbol(m_index.getLocalSymbolName(d->getLocation()), parseLocation);
				}
			}
			else
			{
				const Id symbolId = m_index.getOrCreateSymbolId(type);
				m_index.recordLocation(symbolId, parseLocation, ParseLocationType::QUALIFIER);
			}
		}
		break;
	}
}

void CxxAstVisitorComponentIndexer::beginTraverseTemplateArgumentLoc(
	const clang::TemplateArgumentLoc& loc)
{
	if (getAstVisitor()->shouldVisitReference(loc.getLocation()))
	{
		if (loc.getArgument().getKind() == clang::TemplateArgument::Template)
		{
			// TODO: maybe move this to VisitTemplateName

			const clang::TemplateName templateTemplateArgumentName = loc.getArgument().getAsTemplate();

			const ParseLocation parseLocation = m_index.getParseLocation(loc.getLocation());
			if (templateTemplateArgumentName.isDependent())
			{
				clang::SourceLocation declLocation;
				if (templateTemplateArgumentName.getAsTemplateDecl())
				{
					declLocation = templateTemplateArgumentName.getAsTemplateDecl()->getLocation();
				}
				else
				{
					declLocation = loc.getLocation();
				}
				m_index.recordLocalSymbol(m_index.getLocalSymbolName(declLocation), parseLocation);
			}
			else
			{
				const Id symbolId = m_index.getOrCreateSymbolId(
					templateTemplateArgumentName.getAsTemplateDecl());

				m_index.recordReference(
					ReferenceKind::TYPE_USAGE,
					symbolId,
					m_index.getOrCreateSymbolId(
						m_index.getContext()),
					parseLocation);

				{
					if (const clang::NamedDecl* namedContextDecl =
							m_index.getTopmostContextDecl(1))
					{
						m_index.recordReference(
							ReferenceKind::TYPE_USAGE,
							symbolId,
							m_index.getOrCreateSymbolId(namedContextDecl),	  // we use the closest named decl
																	  // here (e.g. function decl)
							parseLocation);
					}
				}
			}
		}
	}
}

void CxxAstVisitorComponentIndexer::beginTraverseLambdaCapture(
	clang::LambdaExpr* lambdaExpr, const clang::LambdaCapture* capture)
{
	if ((!lambdaExpr->isInitCapture(capture)) && (capture->capturesVariable()))
	{
		clang::ValueDecl* d = capture->getCapturedVar();
		if (utility::isLocalVariable(d) || utility::isParameter(d))
		{
			if (!d->getNameAsString().empty())	  // don't record anonymous parameters
			{
				m_index.recordLocalSymbol(
					m_index.getLocalSymbolName(d->getLocation()), m_index.getParseLocation(capture->getLocation()));
			}
		}
	}
}

void CxxAstVisitorComponentIndexer::visitCastExpr(clang::CastExpr *d)
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

void CxxAstVisitorComponentIndexer::visitCXXFunctionalCastExpr(clang::CXXFunctionalCastExpr *d)
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


void CxxAstVisitorComponentIndexer::visitTagDecl(clang::TagDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		DefinitionKind definitionKind = DefinitionKind::NONE;
		if (d->isThisDeclarationADefinition())
		{
			definitionKind = utility::getDefinitionKind(d);
		}

		const SymbolKind symbolKind = utility::convertTagKind(d->getTagKind());
		const ParseLocation location = m_index.getParseLocation(d->getLocation());

		Id symbolId = m_index.getOrCreateSymbolId(d);
		m_index.recordSymbolKind(symbolId, symbolKind);
		m_index.recordLocation(symbolId, location, ParseLocationType::TOKEN);
		m_index.recordLocation(
			symbolId, m_index.getParseLocationOfTagDeclBody(d), ParseLocationType::SCOPE);
		m_index.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_index.recordDefinitionKind(symbolId, definitionKind);
		m_index.recordDeprecation(symbolId, d);

		if (clang::EnumDecl* enumDecl = clang::dyn_cast_or_null<clang::EnumDecl>(d))
		{
			m_index.recordTemplateMemberSpecialization(
				enumDecl->getMemberSpecializationInfo(), symbolId, location, symbolKind);
		}

		if (clang::CXXRecordDecl* recordDecl = clang::dyn_cast_or_null<clang::CXXRecordDecl>(d))
		{
			m_index.recordTemplateMemberSpecialization(
				recordDecl->getMemberSpecializationInfo(), symbolId, location, symbolKind);
		}
	}
}

void CxxAstVisitorComponentIndexer::visitClassTemplateDecl(clang::ClassTemplateDecl *d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		m_index.concepts().recordTemplateParameterConceptReferences(d);
	}
}

void CxxAstVisitorComponentIndexer::visitClassTemplateSpecializationDecl(
	clang::ClassTemplateSpecializationDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		clang::CXXRecordDecl* specializedFromDecl = nullptr;

		llvm::PointerUnion<clang::ClassTemplateDecl*, clang::ClassTemplatePartialSpecializationDecl*> pu =
			d->getSpecializedTemplateOrPartial();
		if (clang::isa<clang::ClassTemplateDecl*>(pu))
		{
			specializedFromDecl = clang::cast<clang::ClassTemplateDecl*>(pu)->getTemplatedDecl();
		}
		else if (clang::isa<clang::ClassTemplatePartialSpecializationDecl*>(pu))
		{
			specializedFromDecl = clang::cast<clang::ClassTemplatePartialSpecializationDecl*>(pu);
		}

		m_index.recordReference(
			ReferenceKind::TEMPLATE_SPECIALIZATION,
			m_index.getOrCreateSymbolId(specializedFromDecl),
			m_index.getOrCreateSymbolId(d),
			m_index.getParseLocation(d->getLocation()));
	}
}

void CxxAstVisitorComponentIndexer::visitVarDecl(clang::VarDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		// string _varName = d->getNameAsString();
		// string _typeName = d->getType().getAsString();

		// Record auto/deduced types:
		if (const DeducedType *deducedVariableType = d->getType()->getContainedDeducedType())
		{
			for (TypeLoc typeLoc = d->getTypeSourceInfo()->getTypeLoc(); typeLoc; typeLoc = typeLoc.getNextTypeLoc())
			{
				if (const AutoTypeLoc &autoTypeLoc = typeLoc.getAs<AutoTypeLoc>())
				{
					if (const AutoType *autoVariableType = dyn_cast<AutoType>(autoTypeLoc.getTypePtr()))
					{
						if (const auto* conceptDecl = clang_compat::getTypeConstraintConceptDecl(
								autoVariableType))
						{
							// Record concept name location:
							const ParseLocation conceptNameLocation = m_index.getParseLocation(autoTypeLoc.getConceptNameLoc());
							m_index.recordReference(ReferenceKind::USAGE, m_index.getOrCreateSymbolId(conceptDecl), m_index.getOrCreateSymbolId(d), conceptNameLocation);

							// Record 'auto' location:
							const ParseLocation autoKeywordLocation = m_index.getParseLocation(autoTypeLoc.getNameLoc());
							m_index.recordDeducedType(deducedVariableType, m_index.getOrCreateSymbolId(d), autoKeywordLocation);
						}
						else
						{
							// Record keyword location:
							const Id contextSymbolId = m_index.getOrCreateSymbolId(m_index.getContext());
							const ParseLocation autoTypeKeywordLocation = m_index.getParseLocation(autoTypeLoc.getSourceRange());

							m_index.recordDeducedType(deducedVariableType, contextSymbolId, autoTypeKeywordLocation);
						}
					}
				}
			}
		}
		if (utility::isLocalVariable(d) || utility::isParameter(d))
		{
			// Don't record anonymous parameters:
			if (!d->getNameAsString().empty())
			{
				m_index.recordLocalSymbol(m_index.getLocalSymbolName(d->getLocation()), m_index.getParseLocation(d->getLocation()));
			}
		}
		else
		{
			const SymbolKind symbolKind = utility::getSymbolKind(d);
			const ParseLocation location = m_index.getParseLocation(d->getLocation());

			Id symbolId = m_index.getOrCreateSymbolId(d);
			m_index.recordSymbolKind(symbolId, symbolKind);
			m_index.recordLocation(symbolId, location, ParseLocationType::TOKEN);
			m_index.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
			m_index.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
			m_index.recordDeprecation(symbolId, d);

			m_index.recordTemplateMemberSpecialization(d->getMemberSpecializationInfo(), symbolId, location, symbolKind);
		}
	}
}

void CxxAstVisitorComponentIndexer::visitDecompositionDecl(clang::DecompositionDecl *d)
{
	// Record structured bindings:

	if (getAstVisitor()->shouldVisitDecl(d))
	{
		for (const BindingDecl *bindingDecl : d->bindings())
		{
			// Don't record anonymous bindings:
			if (!bindingDecl->getNameAsString().empty())
			{
				m_index.recordLocalSymbol(m_index.getLocalSymbolName(bindingDecl->getLocation()), m_index.getParseLocation(bindingDecl->getLocation()));
			}
		}
	}
}

void CxxAstVisitorComponentIndexer::visitVarTemplateSpecializationDecl(
	clang::VarTemplateSpecializationDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		clang::NamedDecl* specializedFromDecl = nullptr;

		// todo: use context and childcontext!!
		llvm::PointerUnion<clang::VarTemplateDecl*, clang::VarTemplatePartialSpecializationDecl*> pu =
			d->getSpecializedTemplateOrPartial();
		if (clang::isa<clang::VarTemplateDecl*>(pu))
		{
			specializedFromDecl = clang::cast<clang::VarTemplateDecl*>(pu);
		}
		else if (clang::isa<clang::VarTemplatePartialSpecializationDecl*>(pu))
		{
			specializedFromDecl = clang::cast<clang::VarTemplatePartialSpecializationDecl*>(pu);
		}

		m_index.recordReference(
			ReferenceKind::TEMPLATE_SPECIALIZATION,
			m_index.getOrCreateSymbolId(specializedFromDecl),
			m_index.getOrCreateSymbolId(d),
			m_index.getParseLocation(d->getLocation()));
	}
}

void CxxAstVisitorComponentIndexer::visitFieldDecl(clang::FieldDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		if (clang::isa<clang::ObjCIvarDecl>(d))
		{
			return;
		}

		const ParseLocation location = m_index.getParseLocation(d->getLocation());

		Id fieldId = m_index.getOrCreateSymbolId(d);
		m_index.recordSymbolKind(fieldId, SymbolKind::FIELD);
		m_index.recordLocation(fieldId, location, ParseLocationType::TOKEN);
		m_index.recordAccessKind(fieldId, utility::convertAccessSpecifier(d->getAccess()));
		m_index.recordDefinitionKind(fieldId, utility::getDefinitionKind(d));
		m_index.recordDeprecation(fieldId, d);

		if (clang::CXXRecordDecl* declaringRecordDecl =
				clang::dyn_cast_or_null<clang::CXXRecordDecl>(d->getParent()))
		{
			if (clang::CXXRecordDecl* declaringRecordTemplateDecl =
					declaringRecordDecl->getTemplateInstantiationPattern())
			{
				for (clang::FieldDecl* templateFieldDecl: declaringRecordTemplateDecl->fields())
				{
					if (d->getDeclName().isIdentifier() &&
						templateFieldDecl->getDeclName().isIdentifier() &&
						d->getName() == templateFieldDecl->getName())
					{
						Id templateFieldId = m_index.getOrCreateSymbolId(templateFieldDecl);
						m_index.recordSymbolKind(templateFieldId, SymbolKind::FIELD);
						m_index.recordReference(
							ReferenceKind::TEMPLATE_SPECIALIZATION, templateFieldId, fieldId, location);
						break;
					}
				}
			}
		}
	}
}

void CxxAstVisitorComponentIndexer::visitFunctionDecl(clang::FunctionDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_index.getOrCreateSymbolId(d);
		m_index.recordSymbolKind(symbolId, clang::isa<clang::CXXMethodDecl>(d) ? SymbolKind::METHOD : SymbolKind::FUNCTION);
		m_index.recordLocation(symbolId, m_index.getParseLocation(d->getNameInfo().getSourceRange()), ParseLocationType::TOKEN);
		m_index.recordLocation(symbolId, m_index.getParseLocationOfFunctionBody(d), ParseLocationType::SCOPE);
		m_index.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_index.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
		m_index.recordDeprecation(symbolId, d);

		if (d->isFirstDecl())
		{
			m_index.recordLocation(symbolId, m_index.getSignatureLocation(d), ParseLocationType::SIGNATURE);
		}

		if (d->isFunctionTemplateSpecialization())
		{
			if (clang::isa<clang::ClassTemplateSpecializationDecl>(d->getParent()) &&
				!clang::isa<clang::ClassTemplatePartialSpecializationDecl>(d->getParent()) &&
				!clang::dyn_cast<clang::ClassTemplateSpecializationDecl>(d->getParent())->isExplicitSpecialization())
			{
				// record edge from Foo<int>::bar<float>() to Foo<T>::bar<U>() instead of recording
				// an edge from Foo<int>::bar<float>() to Foo<int>::bar<U>() because there is not
				// "written" code for Foo<int>::bar<U>() if Foo<int> is an implicit template
				// specialization.
				if (clang::CXXRecordDecl *declaringRecordDecl = clang::dyn_cast_or_null<clang::CXXRecordDecl>(d->getParent()))
				{
					if (clang::CXXRecordDecl *declaringRecordTemplateDecl = declaringRecordDecl->getTemplateInstantiationPattern())
					{
						for (clang::Decl *templateMethodDecl : declaringRecordTemplateDecl->decls())
						{
							if (clang::FunctionTemplateDecl *functionTemplateDecl = clang::dyn_cast_or_null<clang::FunctionTemplateDecl>(templateMethodDecl))
							{
								if (d->getDeclName().isIdentifier() && functionTemplateDecl->getDeclName().isIdentifier() &&
									d->getName() == functionTemplateDecl->getName())
								{
									const Id templateMethodId = m_index.getOrCreateSymbolId(functionTemplateDecl);
									m_index.recordSymbolKind(templateMethodId, SymbolKind::METHOD);
									m_index.recordReference(ReferenceKind::TEMPLATE_SPECIALIZATION, templateMethodId, symbolId,
										m_index.getParseLocation(d->getLocation()));
									break;
								}
							}
						}
					}
				}
			}
			else
			{
				// record edge from foo<int>() to foo<T>()
				if (FunctionTemplateDecl *primaryTemplate = d->getPrimaryTemplate())
				{
					Id templateId = m_index.getOrCreateSymbolId(primaryTemplate->getTemplatedDecl());
					m_index.recordSymbolKind(templateId, SymbolKind::FUNCTION);
					m_index.recordReference(ReferenceKind::TEMPLATE_SPECIALIZATION, templateId, symbolId, m_index.getParseLocation(d->getLocation()));
				}
			}
		}

		// Record deduced return type:

		if (const DeducedType *deducedReturnType = d->getReturnType()->getContainedDeducedType())
		{
			const SourceRange returnTypeSourceRange = d->getReturnTypeSourceRange();

			if (const AutoType *autoReturnType = dyn_cast<AutoType>(deducedReturnType))
			{
				const Id contextSymbolId = m_index.getOrCreateSymbolId(d);

				if (const auto* conceptDecl = clang_compat::getTypeConstraintConceptDecl(
						autoReturnType))
				{
					// Record the concept reference:
					const ParseLocation conceptNameLocation = m_index.getParseLocation(returnTypeSourceRange.getBegin());
					m_index.recordReference(ReferenceKind::USAGE, m_index.getOrCreateSymbolId(conceptDecl), m_index.getOrCreateSymbolId(d), conceptNameLocation);

					// Record the auto type:
					const ParseLocation autoKeywordLocation = m_index.getParseLocation(returnTypeSourceRange.getEnd());
					m_index.recordDeducedType(deducedReturnType, contextSymbolId, autoKeywordLocation);
				}
				else
				{
					// Record the auto/deduced return type:
					const ParseLocation autoOrDecltypeKeywordLocation = m_index.getParseLocation(returnTypeSourceRange);
					m_index.recordDeducedType(deducedReturnType, contextSymbolId, autoOrDecltypeKeywordLocation);
				}
			}
		}

		m_index.destructorCalls().record(d);
	}
}

void CxxAstVisitorComponentIndexer::visitFunctionTemplateDecl(FunctionTemplateDecl *d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		m_index.concepts().recordTemplateParameterConceptReferences(d);
	}
}

void CxxAstVisitorComponentIndexer::visitCXXMethodDecl(clang::CXXMethodDecl* d)
{
	// Decl has been recorded in VisitFunctionDecl
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_index.getOrCreateSymbolId(d);
		ParseLocation location = m_index.getParseLocation(d->getLocation());

		// TODO: iterate in traversal and use ReferenceKind::OVERRIDE or so..

		for (clang::CXXMethodDecl::method_iterator it = d->begin_overridden_methods(); it != d->end_overridden_methods(); it++)
		{
			Id overrideId = m_index.getOrCreateSymbolId(*it);
			m_index.recordSymbolKind(overrideId, SymbolKind::FUNCTION);
			m_index.recordReference(ReferenceKind::OVERRIDE, overrideId, symbolId, location);
		}

		// record edge from Foo::bar<int>() to Foo::bar<T>()
		m_index.recordTemplateMemberSpecialization(d->getMemberSpecializationInfo(), symbolId, location, SymbolKind::FUNCTION);
	}
}

void CxxAstVisitorComponentIndexer::visitEnumConstantDecl(clang::EnumConstantDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_index.getOrCreateSymbolId(d);
		m_index.recordSymbolKind(symbolId, SymbolKind::ENUM_CONSTANT);
		m_index.recordLocation(symbolId, m_index.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
		m_index.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
		m_index.recordDeprecation(symbolId, d);
	}
}

void CxxAstVisitorComponentIndexer::visitNamespaceDecl(clang::NamespaceDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_index.getOrCreateSymbolId(d);
		m_index.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);
		m_index.recordLocation(symbolId, m_index.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
		m_index.recordLocation(symbolId, m_index.getParseLocation(d->getSourceRange()), ParseLocationType::SCOPE);
		m_index.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_index.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
	}
}

void CxxAstVisitorComponentIndexer::visitNamespaceAliasDecl(clang::NamespaceAliasDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_index.getOrCreateSymbolId(d);
		m_index.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);
		m_index.recordLocation(symbolId, m_index.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
		m_index.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_index.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));

		m_index.recordReference(
			ReferenceKind::USAGE,
			m_index.getOrCreateSymbolId(d->getAliasedNamespace()),
			symbolId,
			m_index.getParseLocation(d->getTargetNameLoc()));

		// TODO: record other namespace as undefined
	}
}

void CxxAstVisitorComponentIndexer::visitTypedefDecl(clang::TypedefDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_index.getOrCreateSymbolId(d);
		m_index.recordSymbolKind(
			symbolId,
			d->getAnonDeclWithTypedefName() == nullptr
				? SymbolKind::TYPEDEF
				: utility::convertTagKind(d->getAnonDeclWithTypedefName()->getTagKind()));
		m_index.recordLocation(symbolId, m_index.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
		m_index.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_index.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
		m_index.recordDeprecation(symbolId, d);
	}
}

void CxxAstVisitorComponentIndexer::visitTypeAliasDecl(clang::TypeAliasDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_index.getOrCreateSymbolId(d);
		m_index.recordSymbolKind(
			symbolId,
			d->getAnonDeclWithTypedefName() == nullptr
				? SymbolKind::TYPEDEF
				: utility::convertTagKind(d->getAnonDeclWithTypedefName()->getTagKind()));
		m_index.recordLocation(symbolId, m_index.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
		m_index.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_index.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
		m_index.recordDeprecation(symbolId, d);
	}
}

void CxxAstVisitorComponentIndexer::visitUsingDirectiveDecl(clang::UsingDirectiveDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_index.getOrCreateSymbolId(d->getNominatedNamespaceAsWritten());
		m_index.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);

		const ParseLocation location = m_index.getParseLocation(d->getLocation());

		m_index.recordReference(
			ReferenceKind::USAGE,
			symbolId,
			m_index.getOrCreateSymbolId(
				m_index.getContext(),
				NameHierarchy(
					getAstVisitor()
						->getCanonicalFilePathCache()
						->getCanonicalFilePath(location.fileId)
						.str(),
					NameDelimiterType::FILE)),
			location);
	}
}

void CxxAstVisitorComponentIndexer::visitUsingDecl(clang::UsingDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		const ParseLocation location = m_index.getParseLocation(d->getLocation());

		m_index.recordReference(
			ReferenceKind::USAGE,
			m_index.getOrCreateSymbolId(d),
			m_index.getOrCreateSymbolId(
				m_index.getContext(),
				NameHierarchy(
					getAstVisitor()
						->getCanonicalFilePathCache()
						->getCanonicalFilePath(location.fileId)
						.str(),
					NameDelimiterType::FILE)),
			location);
	}
}

void CxxAstVisitorComponentIndexer::visitNonTypeTemplateParmDecl(clang::NonTypeTemplateParmDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d) && d->getDeclName().isIdentifier() &&
		!d->getName().empty())	  // We don't create symbols for unnamed template parameters.
	{
		m_index.recordLocalSymbol(
			m_index.getLocalSymbolName(d->getLocation()), m_index.getParseLocation(d->getLocation()));
	}
}

void CxxAstVisitorComponentIndexer::visitTemplateTypeParmDecl(clang::TemplateTypeParmDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d) && d->getDeclName().isIdentifier() &&
		!d->getName().empty())	  // We don't create symbols for unnamed template parameters.
	{
		m_index.recordLocalSymbol(m_index.getLocalSymbolName(d->getLocation()), m_index.getParseLocation(d->getLocation()));

		if (const TypeConstraint *typeConstraint = d->getTypeConstraint())
		{
			m_index.concepts().recordConceptReference(typeConstraint);
		}
	}
}

void CxxAstVisitorComponentIndexer::visitTemplateTemplateParmDecl(clang::TemplateTemplateParmDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d) && d->getDeclName().isIdentifier() &&
		!d->getName().empty())	  // We don't create symbols for unnamed template parameters.
	{
		m_index.recordLocalSymbol(
			m_index.getLocalSymbolName(d->getLocation()), m_index.getParseLocation(d->getLocation()));
	}
}

void CxxAstVisitorComponentIndexer::visitConceptDecl(clang::ConceptDecl *d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		const Id conceptDeclId = m_index.getOrCreateSymbolId(d);

		m_index.recordSymbolKind(conceptDeclId, SymbolKind::CONCEPT);

		m_index.recordAccessKind(conceptDeclId, utility::convertAccessSpecifier(d->getAccess()));

		// Make it 'indexed':
		m_index.recordDefinitionKind(conceptDeclId, utility::getDefinitionKind(d));

		// Make it navigatable/clickable:
		m_index.recordLocation(conceptDeclId, m_index.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
	}
}

void CxxAstVisitorComponentIndexer::visitConceptSpecializationExpr(clang::ConceptSpecializationExpr *d)
{
	if (getAstVisitor()->shouldVisitStmt(d))
	{
		m_index.concepts().recordConceptReference(d);
	}
}

void CxxAstVisitorComponentIndexer::visitConceptReference(clang::ConceptReference *d)
{
	if (getAstVisitor()->shouldVisitReference(d->getLocation()))
	{
		m_index.concepts().recordNamedConceptReference(d);
	}
}

void CxxAstVisitorComponentIndexer::visitTypeLoc(clang::TypeLoc tl)
{
	if (tl.isNull())
	{
		return;
	}

	if ((getAstVisitor()->shouldVisitReference(tl.getBeginLoc())) &&
		(getAstVisitor()->shouldHandleTypeLoc(tl)))
	{
		if (const clang::TemplateTypeParmTypeLoc &ttptl = tl.getAs<clang::TemplateTypeParmTypeLoc>())
		{
			clang::TemplateTypeParmDecl* decl = ttptl.getDecl();
			if (decl)
			{
				m_index.recordLocalSymbol(
					m_index.getLocalSymbolName(decl->getLocation()), m_index.getParseLocation(tl.getBeginLoc()));
			}
		}
		else
		{
			if (const clang::TemplateSpecializationTypeLoc& tstl = tl.getAs<clang::TemplateSpecializationTypeLoc>())
			{
				const clang::TemplateSpecializationType* tst = tstl.getTypePtr();
				if (tst)
				{
					const clang::TemplateName tln = tst->getTemplateName();
					// T<int> where T is a template template PARAMETER is a local
					// symbol of the enclosing template, not a type usage. Other
					// dependent template names (a member template of a dependent
					// base, pre-22 a separate DependentTemplateSpecializationType)
					// resolve to a real symbol and fall through to the recording.
					if (tln.isDependent() &&
						clang::isa_and_nonnull<clang::TemplateTemplateParmDecl>(
							tln.getAsTemplateDecl()))
					{
						m_index.recordLocalSymbol(
							m_index.getLocalSymbolName(tln.getAsTemplateDecl()->getLocation()),
							m_index.getParseLocation(tl.getBeginLoc()));
						return;
					}
				}
			}

			const Id symbolId = m_index.getOrCreateSymbolId(tl.getTypePtr());

			if (clang::dyn_cast_or_null<clang::BuiltinType>(tl.getTypePtr()))
			{
				m_index.recordSymbolKind(symbolId, SymbolKind::BUILTIN_TYPE);
				m_index.recordDefinitionKind(symbolId, DefinitionKind::EXPLICIT);
			}

			// The type's own name token: in LLVM 22+ the TypeLoc includes any
			// qualifier ("test::TestStruct"), so getBeginLoc() would point at the
			// qualifier instead of the name and misplace the type-use location.
			const clang::SourceLocation loc = clang_compat::getTypeLocNameLocation(tl);

			const ParseLocation parseLocation = m_index.getParseLocation(loc);

			m_index.recordReference(m_typeRefKind->isTraversingInheritance() ? ReferenceKind::INHERITANCE : ReferenceKind::TYPE_USAGE,
				symbolId, m_index.getOrCreateSymbolId(m_index.getContext(1)),	// we skip the last element because it refers to this typeloc.
				parseLocation);

			if (m_typeRefKind->isTraversingTemplateArgument())
			{
				if (const clang::NamedDecl* namedContextDecl = m_index.getTopmostContextDecl(2))
				{
					m_index.recordReference(
						ReferenceKind::TYPE_USAGE,
						symbolId,
						m_index.getOrCreateSymbolId(namedContextDecl),	  // we use the closest named decl here
						parseLocation);
				}
			}
		}
	}
}

void CxxAstVisitorComponentIndexer::visitDeclRefExpr(clang::DeclRefExpr* s)
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

void CxxAstVisitorComponentIndexer::visitMemberExpr(clang::MemberExpr* s)
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

void CxxAstVisitorComponentIndexer::visitCXXConstructExpr(clang::CXXConstructExpr* s)
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

void CxxAstVisitorComponentIndexer::visitCXXDeleteExpr(clang::CXXDeleteExpr* s)
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

void CxxAstVisitorComponentIndexer::visitLambdaExpr(clang::LambdaExpr* s)
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

void CxxAstVisitorComponentIndexer::visitConstructorInitializer(clang::CXXCtorInitializer* init)
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

ReferenceKind CxxAstVisitorComponentIndexer::consumeDeclRefContextKind()
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
