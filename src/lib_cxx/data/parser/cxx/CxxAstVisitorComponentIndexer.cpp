#include "CxxAstVisitorComponentIndexer.h"

#include "CanonicalFilePathCache.h"
#include "CxxAstVisitor.h"
#include "CxxAstVisitorComponentContext.h"
#include "CxxAstVisitorComponentDeclRefKind.h"
#include "CxxAstVisitorComponentTypeRefKind.h"
#include "ParserClient.h"
#include "clang_compat/ClangCompat.h"
#include "utilityClang.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Analysis/CFG.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Preprocessor.h>

using namespace std;
using namespace clang;

CxxAstVisitorComponentIndexer::CxxAstVisitorComponentIndexer(
	CxxAstVisitor* astVisitor, clang::ASTContext* astContext, ParserClient& client)
	: CxxAstVisitorComponent(astVisitor)
	, m_astContext(astContext)
	, m_client(client)
	, m_symbols(client, *astVisitor->getCanonicalFilePathCache())
	, m_locations(astVisitor->getLocationExtractor())
{
}

void CxxAstVisitorComponentIndexer::wire()
{
	m_context = getAstVisitor()->getContextComponent();
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

		Id symbolId = m_symbols.getOrCreateSymbolId(namespaceDecl);
		m_client.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);
		m_client.recordLocation(
			symbolId, m_locations.getParseLocation(clang_compat::getNestedNameSpecifierLocalNameLoc(loc)), ParseLocationType::QUALIFIER);

		if (const auto* namespaceAliasDecl =
				clang::dyn_cast<clang::NamespaceAliasDecl>(namespaceDecl))
		{
			symbolId = m_symbols.getOrCreateSymbolId(namespaceAliasDecl->getAliasedNamespace());
			m_client.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);
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
				const Id symbolId = m_symbols.getOrCreateSymbolId(recordDecl);
				m_client.recordSymbolKind(symbolId, symbolKind);
				m_client.recordLocation(
					symbolId, m_locations.getParseLocation(clang_compat::getNestedNameSpecifierLocalNameLoc(loc)), ParseLocationType::QUALIFIER);
			}
		}
		else if (
			const clang::Type* type = clang_compat::getNestedNameSpecifierType(nestedNameSpecifier))
		{
			const ParseLocation parseLocation = m_locations.getParseLocation(clang_compat::getNestedNameSpecifierLocalNameLoc(loc));

			if (const clang::TemplateTypeParmType* tpt =
					clang::dyn_cast_or_null<clang::TemplateTypeParmType>(type))
			{
				clang::TemplateTypeParmDecl* d = tpt->getDecl();
				if (d)
				{
					m_client.recordLocalSymbol(getLocalSymbolName(d->getLocation()), parseLocation);
				}
			}
			else
			{
				const Id symbolId = m_symbols.getOrCreateSymbolId(type);
				m_client.recordLocation(symbolId, parseLocation, ParseLocationType::QUALIFIER);
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

			const ParseLocation parseLocation = m_locations.getParseLocation(loc.getLocation());
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
				m_client.recordLocalSymbol(getLocalSymbolName(declLocation), parseLocation);
			}
			else
			{
				const Id symbolId = m_symbols.getOrCreateSymbolId(
					templateTemplateArgumentName.getAsTemplateDecl());

				m_client.recordReference(
					ReferenceKind::TYPE_USAGE,
					symbolId,
					m_symbols.getOrCreateSymbolId(
						m_context->getContext()),
					parseLocation);

				{
					if (const clang::NamedDecl* namedContextDecl =
							m_context->getTopmostContextDecl(1))
					{
						m_client.recordReference(
							ReferenceKind::TYPE_USAGE,
							symbolId,
							m_symbols.getOrCreateSymbolId(namedContextDecl),	  // we use the closest named decl
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
				m_client.recordLocalSymbol(
					getLocalSymbolName(d->getLocation()), m_locations.getParseLocation(capture->getLocation()));
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
			const Id referencedSymbolId = m_symbols.getOrCreateSymbolId(d->getConversionFunction());
			const Id contextSymbolId = m_symbols.getOrCreateSymbolId(m_context->getContext());
			const ParseLocation location = m_locations.getParseLocation(d->getSourceRange());

			m_client.recordReference(ReferenceKind::CALL, referencedSymbolId, contextSymbolId, location);
		}
	}
}

void CxxAstVisitorComponentIndexer::visitCXXFunctionalCastExpr(clang::CXXFunctionalCastExpr *d)
{
	if (getAstVisitor()->shouldVisitStmt(d))
	{
		if (QualType qualType = d->getType(); !qualType.isNull())
		{
			const Id contextSymbolId = m_symbols.getOrCreateSymbolId(m_context->getContext());
			recordDeducedQualType(qualType, contextSymbolId, m_locations.getParseLocation(d->getBeginLoc()));
		}
	}
}


void CxxAstVisitorComponentIndexer::recordDeprecation(Id symbolId, const clang::Decl* d)
{
	if (const DeprecatedAttr* attr = d->getAttr<DeprecatedAttr>())
	{
		m_client.recordNodeModifier(symbolId, NODE_MODIFIER_DEPRECATED);
		// The bit carries the boolean; the message (if any) rides in a DEPRECATED
		// node_attribute — same split the Swift/Rust producers use.
		const std::string message = attr->getMessage().str();
		if (!message.empty())
		{
			m_client.recordNodeAttribute(symbolId, NodeAttributeKind::DEPRECATED, message);
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
		const ParseLocation location = m_locations.getParseLocation(d->getLocation());

		Id symbolId = m_symbols.getOrCreateSymbolId(d);
		m_client.recordSymbolKind(symbolId, symbolKind);
		m_client.recordLocation(symbolId, location, ParseLocationType::TOKEN);
		m_client.recordLocation(
			symbolId, m_locations.getParseLocationOfTagDeclBody(d), ParseLocationType::SCOPE);
		m_client.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_client.recordDefinitionKind(symbolId, definitionKind);
		recordDeprecation(symbolId, d);

		if (clang::EnumDecl* enumDecl = clang::dyn_cast_or_null<clang::EnumDecl>(d))
		{
			recordTemplateMemberSpecialization(
				enumDecl->getMemberSpecializationInfo(), symbolId, location, symbolKind);
		}

		if (clang::CXXRecordDecl* recordDecl = clang::dyn_cast_or_null<clang::CXXRecordDecl>(d))
		{
			recordTemplateMemberSpecialization(
				recordDecl->getMemberSpecializationInfo(), symbolId, location, symbolKind);
		}
	}
}

void CxxAstVisitorComponentIndexer::visitClassTemplateDecl(clang::ClassTemplateDecl *d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		recordTemplateParameterConceptReferences(d);
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

		m_client.recordReference(
			ReferenceKind::TEMPLATE_SPECIALIZATION,
			m_symbols.getOrCreateSymbolId(specializedFromDecl),
			m_symbols.getOrCreateSymbolId(d),
			m_locations.getParseLocation(d->getLocation()));
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
							const ParseLocation conceptNameLocation = m_locations.getParseLocation(autoTypeLoc.getConceptNameLoc());
							m_client.recordReference(ReferenceKind::USAGE, m_symbols.getOrCreateSymbolId(conceptDecl), m_symbols.getOrCreateSymbolId(d), conceptNameLocation);

							// Record 'auto' location:
							const ParseLocation autoKeywordLocation = m_locations.getParseLocation(autoTypeLoc.getNameLoc());
							recordDeducedType(deducedVariableType, m_symbols.getOrCreateSymbolId(d), autoKeywordLocation);
						}
						else
						{
							// Record keyword location:
							const Id contextSymbolId = m_symbols.getOrCreateSymbolId(m_context->getContext());
							const ParseLocation autoTypeKeywordLocation = m_locations.getParseLocation(autoTypeLoc.getSourceRange());

							recordDeducedType(deducedVariableType, contextSymbolId, autoTypeKeywordLocation);
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
				m_client.recordLocalSymbol(getLocalSymbolName(d->getLocation()), m_locations.getParseLocation(d->getLocation()));
			}
		}
		else
		{
			const SymbolKind symbolKind = utility::getSymbolKind(d);
			const ParseLocation location = m_locations.getParseLocation(d->getLocation());

			Id symbolId = m_symbols.getOrCreateSymbolId(d);
			m_client.recordSymbolKind(symbolId, symbolKind);
			m_client.recordLocation(symbolId, location, ParseLocationType::TOKEN);
			m_client.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
			m_client.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
			recordDeprecation(symbolId, d);

			recordTemplateMemberSpecialization(d->getMemberSpecializationInfo(), symbolId, location, symbolKind);
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
				m_client.recordLocalSymbol(getLocalSymbolName(bindingDecl->getLocation()), m_locations.getParseLocation(bindingDecl->getLocation()));
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

		m_client.recordReference(
			ReferenceKind::TEMPLATE_SPECIALIZATION,
			m_symbols.getOrCreateSymbolId(specializedFromDecl),
			m_symbols.getOrCreateSymbolId(d),
			m_locations.getParseLocation(d->getLocation()));
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

		const ParseLocation location = m_locations.getParseLocation(d->getLocation());

		Id fieldId = m_symbols.getOrCreateSymbolId(d);
		m_client.recordSymbolKind(fieldId, SymbolKind::FIELD);
		m_client.recordLocation(fieldId, location, ParseLocationType::TOKEN);
		m_client.recordAccessKind(fieldId, utility::convertAccessSpecifier(d->getAccess()));
		m_client.recordDefinitionKind(fieldId, utility::getDefinitionKind(d));
		recordDeprecation(fieldId, d);

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
						Id templateFieldId = m_symbols.getOrCreateSymbolId(templateFieldDecl);
						m_client.recordSymbolKind(templateFieldId, SymbolKind::FIELD);
						m_client.recordReference(
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
		Id symbolId = m_symbols.getOrCreateSymbolId(d);
		m_client.recordSymbolKind(symbolId, clang::isa<clang::CXXMethodDecl>(d) ? SymbolKind::METHOD : SymbolKind::FUNCTION);
		m_client.recordLocation(symbolId, m_locations.getParseLocation(d->getNameInfo().getSourceRange()), ParseLocationType::TOKEN);
		m_client.recordLocation(symbolId, m_locations.getParseLocationOfFunctionBody(d), ParseLocationType::SCOPE);
		m_client.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_client.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
		recordDeprecation(symbolId, d);

		if (d->isFirstDecl())
		{
			m_client.recordLocation(symbolId, m_locations.getSignatureLocation(d), ParseLocationType::SIGNATURE);
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
									const Id templateMethodId = m_symbols.getOrCreateSymbolId(functionTemplateDecl);
									m_client.recordSymbolKind(templateMethodId, SymbolKind::METHOD);
									m_client.recordReference(ReferenceKind::TEMPLATE_SPECIALIZATION, templateMethodId, symbolId,
										m_locations.getParseLocation(d->getLocation()));
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
					Id templateId = m_symbols.getOrCreateSymbolId(primaryTemplate->getTemplatedDecl());
					m_client.recordSymbolKind(templateId, SymbolKind::FUNCTION);
					m_client.recordReference(ReferenceKind::TEMPLATE_SPECIALIZATION, templateId, symbolId, m_locations.getParseLocation(d->getLocation()));
				}
			}
		}

		// Record deduced return type:

		if (const DeducedType *deducedReturnType = d->getReturnType()->getContainedDeducedType())
		{
			const SourceRange returnTypeSourceRange = d->getReturnTypeSourceRange();

			if (const AutoType *autoReturnType = dyn_cast<AutoType>(deducedReturnType))
			{
				const Id contextSymbolId = m_symbols.getOrCreateSymbolId(d);

				if (const auto* conceptDecl = clang_compat::getTypeConstraintConceptDecl(
						autoReturnType))
				{
					// Record the concept reference:
					const ParseLocation conceptNameLocation = m_locations.getParseLocation(returnTypeSourceRange.getBegin());
					m_client.recordReference(ReferenceKind::USAGE, m_symbols.getOrCreateSymbolId(conceptDecl), m_symbols.getOrCreateSymbolId(d), conceptNameLocation);

					// Record the auto type:
					const ParseLocation autoKeywordLocation = m_locations.getParseLocation(returnTypeSourceRange.getEnd());
					recordDeducedType(deducedReturnType, contextSymbolId, autoKeywordLocation);
				}
				else
				{
					// Record the auto/deduced return type:
					const ParseLocation autoOrDecltypeKeywordLocation = m_locations.getParseLocation(returnTypeSourceRange);
					recordDeducedType(deducedReturnType, contextSymbolId, autoOrDecltypeKeywordLocation);
				}
			}
		}

		recordNonTrivialDestructorCalls(d);
	}
}

void CxxAstVisitorComponentIndexer::visitFunctionTemplateDecl(FunctionTemplateDecl *d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		recordTemplateParameterConceptReferences(d);
	}
}

void CxxAstVisitorComponentIndexer::visitCXXMethodDecl(clang::CXXMethodDecl* d)
{
	// Decl has been recorded in VisitFunctionDecl
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_symbols.getOrCreateSymbolId(d);
		ParseLocation location = m_locations.getParseLocation(d->getLocation());

		// TODO: iterate in traversal and use ReferenceKind::OVERRIDE or so..

		for (clang::CXXMethodDecl::method_iterator it = d->begin_overridden_methods(); it != d->end_overridden_methods(); it++)
		{
			Id overrideId = m_symbols.getOrCreateSymbolId(*it);
			m_client.recordSymbolKind(overrideId, SymbolKind::FUNCTION);
			m_client.recordReference(ReferenceKind::OVERRIDE, overrideId, symbolId, location);
		}

		// record edge from Foo::bar<int>() to Foo::bar<T>()
		recordTemplateMemberSpecialization(d->getMemberSpecializationInfo(), symbolId, location, SymbolKind::FUNCTION);
	}
}

void CxxAstVisitorComponentIndexer::visitEnumConstantDecl(clang::EnumConstantDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_symbols.getOrCreateSymbolId(d);
		m_client.recordSymbolKind(symbolId, SymbolKind::ENUM_CONSTANT);
		m_client.recordLocation(symbolId, m_locations.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
		m_client.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
		recordDeprecation(symbolId, d);
	}
}

void CxxAstVisitorComponentIndexer::visitNamespaceDecl(clang::NamespaceDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_symbols.getOrCreateSymbolId(d);
		m_client.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);
		m_client.recordLocation(symbolId, m_locations.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
		m_client.recordLocation(symbolId, m_locations.getParseLocation(d->getSourceRange()), ParseLocationType::SCOPE);
		m_client.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_client.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
	}
}

void CxxAstVisitorComponentIndexer::visitNamespaceAliasDecl(clang::NamespaceAliasDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_symbols.getOrCreateSymbolId(d);
		m_client.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);
		m_client.recordLocation(symbolId, m_locations.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
		m_client.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_client.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));

		m_client.recordReference(
			ReferenceKind::USAGE,
			m_symbols.getOrCreateSymbolId(d->getAliasedNamespace()),
			symbolId,
			m_locations.getParseLocation(d->getTargetNameLoc()));

		// TODO: record other namespace as undefined
	}
}

void CxxAstVisitorComponentIndexer::visitTypedefDecl(clang::TypedefDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_symbols.getOrCreateSymbolId(d);
		m_client.recordSymbolKind(
			symbolId,
			d->getAnonDeclWithTypedefName() == nullptr
				? SymbolKind::TYPEDEF
				: utility::convertTagKind(d->getAnonDeclWithTypedefName()->getTagKind()));
		m_client.recordLocation(symbolId, m_locations.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
		m_client.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_client.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
		recordDeprecation(symbolId, d);
	}
}

void CxxAstVisitorComponentIndexer::visitTypeAliasDecl(clang::TypeAliasDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_symbols.getOrCreateSymbolId(d);
		m_client.recordSymbolKind(
			symbolId,
			d->getAnonDeclWithTypedefName() == nullptr
				? SymbolKind::TYPEDEF
				: utility::convertTagKind(d->getAnonDeclWithTypedefName()->getTagKind()));
		m_client.recordLocation(symbolId, m_locations.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
		m_client.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
		m_client.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
		recordDeprecation(symbolId, d);
	}
}

void CxxAstVisitorComponentIndexer::visitUsingDirectiveDecl(clang::UsingDirectiveDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		Id symbolId = m_symbols.getOrCreateSymbolId(d->getNominatedNamespaceAsWritten());
		m_client.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);

		const ParseLocation location = m_locations.getParseLocation(d->getLocation());

		m_client.recordReference(
			ReferenceKind::USAGE,
			symbolId,
			m_symbols.getOrCreateSymbolId(
				m_context->getContext(),
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
		const ParseLocation location = m_locations.getParseLocation(d->getLocation());

		m_client.recordReference(
			ReferenceKind::USAGE,
			m_symbols.getOrCreateSymbolId(d),
			m_symbols.getOrCreateSymbolId(
				m_context->getContext(),
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
		m_client.recordLocalSymbol(
			getLocalSymbolName(d->getLocation()), m_locations.getParseLocation(d->getLocation()));
	}
}

void CxxAstVisitorComponentIndexer::visitTemplateTypeParmDecl(clang::TemplateTypeParmDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d) && d->getDeclName().isIdentifier() &&
		!d->getName().empty())	  // We don't create symbols for unnamed template parameters.
	{
		m_client.recordLocalSymbol(getLocalSymbolName(d->getLocation()), m_locations.getParseLocation(d->getLocation()));

		if (const TypeConstraint *typeConstraint = d->getTypeConstraint())
		{
			recordConceptReference(typeConstraint);
		}
	}
}

void CxxAstVisitorComponentIndexer::visitTemplateTemplateParmDecl(clang::TemplateTemplateParmDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d) && d->getDeclName().isIdentifier() &&
		!d->getName().empty())	  // We don't create symbols for unnamed template parameters.
	{
		m_client.recordLocalSymbol(
			getLocalSymbolName(d->getLocation()), m_locations.getParseLocation(d->getLocation()));
	}
}

void CxxAstVisitorComponentIndexer::visitConceptDecl(clang::ConceptDecl *d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		const Id conceptDeclId = m_symbols.getOrCreateSymbolId(d);

		m_client.recordSymbolKind(conceptDeclId, SymbolKind::CONCEPT);

		m_client.recordAccessKind(conceptDeclId, utility::convertAccessSpecifier(d->getAccess()));

		// Make it 'indexed':
		m_client.recordDefinitionKind(conceptDeclId, utility::getDefinitionKind(d));

		// Make it navigatable/clickable:
		m_client.recordLocation(conceptDeclId, m_locations.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
	}
}

void CxxAstVisitorComponentIndexer::visitConceptSpecializationExpr(clang::ConceptSpecializationExpr *d)
{
	if (getAstVisitor()->shouldVisitStmt(d))
	{
		recordConceptReference(d);
	}
}

void CxxAstVisitorComponentIndexer::visitConceptReference(clang::ConceptReference *d)
{
	if (getAstVisitor()->shouldVisitReference(d->getLocation()))
	{
		recordNamedConceptReference(d);
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
				m_client.recordLocalSymbol(
					getLocalSymbolName(decl->getLocation()), m_locations.getParseLocation(tl.getBeginLoc()));
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
						m_client.recordLocalSymbol(
							getLocalSymbolName(tln.getAsTemplateDecl()->getLocation()),
							m_locations.getParseLocation(tl.getBeginLoc()));
						return;
					}
				}
			}

			const Id symbolId = m_symbols.getOrCreateSymbolId(tl.getTypePtr());

			if (clang::dyn_cast_or_null<clang::BuiltinType>(tl.getTypePtr()))
			{
				m_client.recordSymbolKind(symbolId, SymbolKind::BUILTIN_TYPE);
				m_client.recordDefinitionKind(symbolId, DefinitionKind::EXPLICIT);
			}

			// The type's own name token: in LLVM 22+ the TypeLoc includes any
			// qualifier ("test::TestStruct"), so getBeginLoc() would point at the
			// qualifier instead of the name and misplace the type-use location.
			const clang::SourceLocation loc = clang_compat::getTypeLocNameLocation(tl);

			const ParseLocation parseLocation = m_locations.getParseLocation(loc);

			m_client.recordReference(m_typeRefKind->isTraversingInheritance() ? ReferenceKind::INHERITANCE : ReferenceKind::TYPE_USAGE,
				symbolId, m_symbols.getOrCreateSymbolId(m_context->getContext(1)),	// we skip the last element because it refers to this typeloc.
				parseLocation);

			if (m_typeRefKind->isTraversingTemplateArgument())
			{
				if (const clang::NamedDecl* namedContextDecl = m_context->getTopmostContextDecl(2))
				{
					m_client.recordReference(
						ReferenceKind::TYPE_USAGE,
						symbolId,
						m_symbols.getOrCreateSymbolId(namedContextDecl),	  // we use the closest named decl here
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
				m_client.recordLocalSymbol(getLocalSymbolName(decl->getLocation()), m_locations.getParseLocation(s->getLocation()));
			}
		}
		// Check for template parameter:
		else if (clang::isa<clang::NonTypeTemplateParmDecl>(decl) || clang::isa<clang::TemplateTypeParmDecl>(decl) || clang::isa<clang::TemplateTemplateParmDecl>(decl))
		{
			m_client.recordLocalSymbol(getLocalSymbolName(decl->getLocation()), m_locations.getParseLocation(s->getLocation()));
		}
		// Check for structured binding declaration:
		else if (clang::isa<clang::BindingDecl>(decl))
		{
			// Without this line the structured binding variables are not indexed as local variables!
			m_client.recordLocalSymbol(getLocalSymbolName(decl->getLocation()), m_locations.getParseLocation(s->getLocation()));
		}
		else
		{
			Id symbolId = m_symbols.getOrCreateSymbolId(decl);

			const ReferenceKind refKind = consumeDeclRefContextKind();
			if (refKind == ReferenceKind::CALL)
			{
				m_client.recordSymbolKind(symbolId, SymbolKind::FUNCTION);
			}
			const Id contextSymbolId = m_symbols.getOrCreateSymbolId(m_context->getContext());
			m_client.recordReference(refKind, symbolId, contextSymbolId, m_locations.getParseLocation(s->getLocation()));
		}
	}
}

void CxxAstVisitorComponentIndexer::visitMemberExpr(clang::MemberExpr* s)
{
	if (getAstVisitor()->shouldVisitReference(s->getMemberLoc()))
	{
		const Id symbolId = m_symbols.getOrCreateSymbolId(s->getMemberDecl());
		const Id contextSymbolId = m_symbols.getOrCreateSymbolId(m_context->getContext());
		const ReferenceKind refKind = consumeDeclRefContextKind();

		if (refKind == ReferenceKind::CALL)
		{
			m_client.recordSymbolKind(symbolId, SymbolKind::FUNCTION);
		}
		m_client.recordReference(refKind, symbolId, contextSymbolId, m_locations.getParseLocation(s->getMemberLoc()));
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

		const Id symbolId = m_symbols.getOrCreateSymbolId(s->getConstructor());

		const ReferenceKind refKind = consumeDeclRefContextKind();
		if (refKind == ReferenceKind::CALL)
		{
			m_client.recordSymbolKind(symbolId, SymbolKind::FUNCTION);
		}

		m_client.recordReference(
			refKind,
			symbolId,
			m_symbols.getOrCreateSymbolId(
				m_context->getContext()),
			m_locations.getParseLocation(loc));
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
					const Id symbolId = m_symbols.getOrCreateSymbolId(destructorDecl);

					m_client.recordReference(
						ReferenceKind::CALL,
						symbolId,
						m_symbols.getOrCreateSymbolId(
							m_context->getContext()),
						m_locations.getParseLocation(s->getBeginLoc()));
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
			Id symbolId = m_symbols.getOrCreateSymbolId(methodDecl);
			m_client.recordSymbolKind(symbolId, SymbolKind::FUNCTION);
			m_client.recordLocation(symbolId, m_locations.getParseLocation(s->getBeginLoc()), ParseLocationType::TOKEN);
			m_client.recordLocation(symbolId, m_locations.getParseLocationOfFunctionBody(methodDecl), ParseLocationType::SCOPE);
			m_client.recordDefinitionKind(symbolId, utility::getDefinitionKind(methodDecl));
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
						recordTemplateParameterConceptReferences(functionTemplateDecl);
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
			m_client.recordReference(
				ReferenceKind::USAGE,
				m_symbols.getOrCreateSymbolId(memberDecl),
				m_symbols.getOrCreateSymbolId(
					m_context->getContext()),
				m_locations.getParseLocation(init->getMemberLocation()));
		}
	}
}

void CxxAstVisitorComponentIndexer::recordTemplateMemberSpecialization(
	const clang::MemberSpecializationInfo* memberSpecializationInfo,
	Id contextId,
	const ParseLocation& location,
	SymbolKind symbolKind)
{
	if (memberSpecializationInfo != nullptr)
	{
		Id symbolId = m_symbols.getOrCreateSymbolId(memberSpecializationInfo->getInstantiatedFrom());
		m_client.recordSymbolKind(symbolId, symbolKind);
		m_client.recordReference(ReferenceKind::TEMPLATE_SPECIALIZATION, symbolId, contextId, location);
	}
}

void CxxAstVisitorComponentIndexer::recordTemplateParameterConceptReferences(const TemplateDecl *templateDecl)
{
	if (const TemplateParameterList *templateParameters = templateDecl->getTemplateParameters())
	{
		for (const NamedDecl *namedDecl : *templateParameters)
		{
			if (const TemplateTypeParmDecl *templateTypeParmDecl = dyn_cast<TemplateTypeParmDecl>(namedDecl))
			{
				if (const TypeConstraint *typeConstraint = templateTypeParmDecl->getTypeConstraint())
				{
					recordConceptReference(typeConstraint);
				}
			}
		}
	}
}

template <typename T>
void CxxAstVisitorComponentIndexer::recordConceptReference(const T *d)
{
	if (const ConceptReference *conceptReference = d->getConceptReference())
	{
		recordNamedConceptReference(conceptReference);
	}
}

void CxxAstVisitorComponentIndexer::recordNamedConceptReference(const ConceptReference *conceptReference)
{
	if (const auto* conceptDecl = clang_compat::getNamedConceptDecl(conceptReference))
	{
		const Id conceptDeclId = m_symbols.getOrCreateSymbolId(conceptDecl);
		const Id contextSymbolId = m_symbols.getOrCreateSymbolId(m_context->getContext());
		const ParseLocation conceptNameLocation = m_locations.getParseLocation(conceptReference->getLocation());

		m_client.recordReference(ReferenceKind::USAGE, conceptDeclId, contextSymbolId, conceptNameLocation);
	}
}

void CxxAstVisitorComponentIndexer::recordDeducedType(const DeducedType *containedDeducedType, const Id contextSymbolId, const ParseLocation &keywordLocation)
{
	if (QualType deducedType = containedDeducedType->getDeducedType(); !deducedType.isNull())
	{
		recordDeducedQualType(deducedType, contextSymbolId, keywordLocation);
	}
}

void CxxAstVisitorComponentIndexer::recordDeducedQualType(const QualType deducedQualType, const Id contextSymbolId, const ParseLocation &keywordLocation)
{
	// Record the deduced type location:
	const Id deducedTypeId = m_symbols.getOrCreateSymbolId(deducedQualType.getTypePtr());

	m_client.recordDefinitionKind(deducedTypeId, DefinitionKind::EXPLICIT);
	m_client.recordReference(ReferenceKind::TYPE_USAGE, deducedTypeId, contextSymbolId, keywordLocation);
}

void CxxAstVisitorComponentIndexer::recordNonTrivialDestructorCalls(const FunctionDecl *functionDecl)
{
	auto recordDestructorCall = [this](const FunctionDecl *functionDecl, const CXXDestructorDecl *destructorDecl)
	{
		Id referencedSymbolId = m_symbols.getOrCreateSymbolId(destructorDecl);
		Id contextSymbolId = m_symbols.getOrCreateSymbolId(m_context->getContext());
		// functionDecl->getLocation: The function name
		// functionDecl->getBeginLoc: Begin of function
		// functionDecl->getEndLoc: End of function
		// functionDecl->getSourceRange: The complete function
		// functionDecl->getDefaultLoc: No location but 'call' edge
		// destructorDecl->getSourceRange: The destructor itself

		ParseLocation parseLocation = m_locations.getParseLocation(functionDecl->getEndLoc());
		m_client.recordReference(ReferenceKind::CALL, referencedSymbolId, contextSymbolId, parseLocation);
	};

	// Adapted from:
	// "How to get information about call to destructors in Clang LibTooling?"
	// https://stackoverflow.com/questions/59610156/how-to-get-information-about-call-to-destructors-in-clang-libtooling

	if (functionDecl->isThisDeclarationADefinition())
	{
		CFG::BuildOptions buildOptions;
		buildOptions.AddImplicitDtors = true;
		buildOptions.AddTemporaryDtors = true;

		if (unique_ptr<CFG> cfg = CFG::buildCFG(functionDecl, functionDecl->getBody(), m_astContext, buildOptions))
		{
			for (CFGBlock *block : cfg->const_nodes())
			{
				for (auto ref : block->refs())
				{
					// It should not be necessary to special-case 'CFGBaseDtor'. But 'CFGImplicitDtor::getDestructorDecl'
					// is simply missing the implementation of that case. See 'CFGImplicitDtor::getDestructorDecl()':
					// https://github.com/llvm/llvm-project/blob/a0b8d548fd250c92c8f9274b57e38ad3f0b215e9/clang/lib/Analysis/CFG.cpp#L5465
					if (optional<CFGBaseDtor> baseDtor = ref->getAs<CFGBaseDtor>())
					{
						const CXXBaseSpecifier *baseSpec = baseDtor->getBaseSpecifier();
						if (const RecordType *recordType = dyn_cast<RecordType>(baseSpec->getType().getDesugaredType(*m_astContext).getTypePtr()))
						{
							if (const CXXRecordDecl *recordDecl = dyn_cast<CXXRecordDecl>(recordType->getDecl()))
							{
								if (const CXXDestructorDecl *dtorDecl = recordDecl->getDestructor())
								{
									recordDestructorCall(functionDecl, dtorDecl);
								}
							}
						}
					}
					// If it were not for the above unimplemented functionality, we would only need
					// this block.
					else if (optional<CFGImplicitDtor> implicitDtor = ref->getAs<CFGImplicitDtor>())
					{
						if (const CXXDestructorDecl *dtorDecl = implicitDtor->getDestructorDecl(*m_astContext))
						{
							recordDestructorCall(functionDecl, dtorDecl);
						}
					}
				}
			}
		}
	}
}

std::string CxxAstVisitorComponentIndexer::getLocalSymbolName(const clang::SourceLocation& loc) const
{
	const ParseLocation location = m_locations.getParseLocation(loc);
	return getAstVisitor()->getCanonicalFilePathCache()->getCanonicalFilePath(location.fileId).fileName() +
		"<" + std::to_string(location.startLineNumber) + ":" +
		std::to_string(location.startColumnNumber) + ">";
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
