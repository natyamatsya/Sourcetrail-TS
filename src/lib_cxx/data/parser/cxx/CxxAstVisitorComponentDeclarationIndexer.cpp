#include "CxxAstVisitorComponentDeclarationIndexer.h"

#include "CanonicalFilePathCache.h"
#include "CxxAstVisitor.h"
#include "CxxIndexingContext.h"
#include "NameHierarchy.h"
#include "clang_compat/ClangCompat.h"
#include "utilityClang.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>


CxxAstVisitorComponentDeclarationIndexer::CxxAstVisitorComponentDeclarationIndexer(
	CxxAstVisitor* astVisitor, CxxIndexingContext& index)
	: CxxAstVisitorComponent(astVisitor)
	, m_index(index)
{
}

void CxxAstVisitorComponentDeclarationIndexer::visitTagDecl(clang::TagDecl* d)
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

void CxxAstVisitorComponentDeclarationIndexer::visitClassTemplateDecl(clang::ClassTemplateDecl *d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		m_index.concepts().recordTemplateParameterConceptReferences(d);
	}
}

void CxxAstVisitorComponentDeclarationIndexer::visitClassTemplateSpecializationDecl(
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

void CxxAstVisitorComponentDeclarationIndexer::visitVarDecl(clang::VarDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		// string _varName = d->getNameAsString();
		// string _typeName = d->getType().getAsString();

		// Record auto/deduced types:
		if (const clang::DeducedType *deducedVariableType = d->getType()->getContainedDeducedType())
		{
			for (clang::TypeLoc typeLoc = d->getTypeSourceInfo()->getTypeLoc(); typeLoc; typeLoc = typeLoc.getNextTypeLoc())
			{
				if (const clang::AutoTypeLoc &autoTypeLoc = typeLoc.getAs<clang::AutoTypeLoc>())
				{
					if (const clang::AutoType *autoVariableType = dyn_cast<clang::AutoType>(autoTypeLoc.getTypePtr()))
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

void CxxAstVisitorComponentDeclarationIndexer::visitDecompositionDecl(clang::DecompositionDecl *d)
{
	// Record structured bindings:

	if (getAstVisitor()->shouldVisitDecl(d))
	{
		for (const clang::BindingDecl *bindingDecl : d->bindings())
		{
			// Don't record anonymous bindings:
			if (!bindingDecl->getNameAsString().empty())
			{
				m_index.recordLocalSymbol(m_index.getLocalSymbolName(bindingDecl->getLocation()), m_index.getParseLocation(bindingDecl->getLocation()));
			}
		}
	}
}

void CxxAstVisitorComponentDeclarationIndexer::visitVarTemplateSpecializationDecl(
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

void CxxAstVisitorComponentDeclarationIndexer::visitFieldDecl(clang::FieldDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		if (clang::isa<clang::ObjCIvarDecl>(d))
		{
			return;
		}

		const Id fieldId = m_index.recordDeclaration(d, SymbolKind::FIELD);

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
							ReferenceKind::TEMPLATE_SPECIALIZATION,
							templateFieldId,
							fieldId,
							m_index.getParseLocation(d->getLocation()));
						break;
					}
				}
			}
		}
	}
}

void CxxAstVisitorComponentDeclarationIndexer::visitFunctionDecl(clang::FunctionDecl* d)
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
				if (clang::FunctionTemplateDecl *primaryTemplate = d->getPrimaryTemplate())
				{
					Id templateId = m_index.getOrCreateSymbolId(primaryTemplate->getTemplatedDecl());
					m_index.recordSymbolKind(templateId, SymbolKind::FUNCTION);
					m_index.recordReference(ReferenceKind::TEMPLATE_SPECIALIZATION, templateId, symbolId, m_index.getParseLocation(d->getLocation()));
				}
			}
		}

		// Record deduced return type:

		if (const clang::DeducedType *deducedReturnType = d->getReturnType()->getContainedDeducedType())
		{
			const clang::SourceRange returnTypeSourceRange = d->getReturnTypeSourceRange();

			if (const clang::AutoType *autoReturnType = dyn_cast<clang::AutoType>(deducedReturnType))
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

void CxxAstVisitorComponentDeclarationIndexer::visitFunctionTemplateDecl(clang::FunctionTemplateDecl *d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		m_index.concepts().recordTemplateParameterConceptReferences(d);
	}
}

void CxxAstVisitorComponentDeclarationIndexer::visitCXXMethodDecl(clang::CXXMethodDecl* d)
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

void CxxAstVisitorComponentDeclarationIndexer::visitEnumConstantDecl(clang::EnumConstantDecl* d)
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

void CxxAstVisitorComponentDeclarationIndexer::visitNamespaceDecl(clang::NamespaceDecl* d)
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

void CxxAstVisitorComponentDeclarationIndexer::visitNamespaceAliasDecl(clang::NamespaceAliasDecl* d)
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

void CxxAstVisitorComponentDeclarationIndexer::visitTypedefDecl(clang::TypedefDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		const SymbolKind symbolKind = d->getAnonDeclWithTypedefName() == nullptr
			? SymbolKind::TYPEDEF
			: utility::convertTagKind(d->getAnonDeclWithTypedefName()->getTagKind());
		m_index.recordDeclaration(d, symbolKind);
	}
}

void CxxAstVisitorComponentDeclarationIndexer::visitTypeAliasDecl(clang::TypeAliasDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d))
	{
		const SymbolKind symbolKind = d->getAnonDeclWithTypedefName() == nullptr
			? SymbolKind::TYPEDEF
			: utility::convertTagKind(d->getAnonDeclWithTypedefName()->getTagKind());
		m_index.recordDeclaration(d, symbolKind);
	}
}

void CxxAstVisitorComponentDeclarationIndexer::visitUsingDirectiveDecl(clang::UsingDirectiveDecl* d)
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

void CxxAstVisitorComponentDeclarationIndexer::visitUsingDecl(clang::UsingDecl* d)
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

void CxxAstVisitorComponentDeclarationIndexer::visitNonTypeTemplateParmDecl(clang::NonTypeTemplateParmDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d) && d->getDeclName().isIdentifier() &&
		!d->getName().empty())	  // We don't create symbols for unnamed template parameters.
	{
		m_index.recordLocalSymbol(
			m_index.getLocalSymbolName(d->getLocation()), m_index.getParseLocation(d->getLocation()));
	}
}

void CxxAstVisitorComponentDeclarationIndexer::visitTemplateTypeParmDecl(clang::TemplateTypeParmDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d) && d->getDeclName().isIdentifier() &&
		!d->getName().empty())	  // We don't create symbols for unnamed template parameters.
	{
		m_index.recordLocalSymbol(m_index.getLocalSymbolName(d->getLocation()), m_index.getParseLocation(d->getLocation()));

		if (const clang::TypeConstraint *typeConstraint = d->getTypeConstraint())
		{
			m_index.concepts().recordConceptReference(typeConstraint);
		}
	}
}

void CxxAstVisitorComponentDeclarationIndexer::visitTemplateTemplateParmDecl(clang::TemplateTemplateParmDecl* d)
{
	if (getAstVisitor()->shouldVisitDecl(d) && d->getDeclName().isIdentifier() &&
		!d->getName().empty())	  // We don't create symbols for unnamed template parameters.
	{
		m_index.recordLocalSymbol(
			m_index.getLocalSymbolName(d->getLocation()), m_index.getParseLocation(d->getLocation()));
	}
}

void CxxAstVisitorComponentDeclarationIndexer::visitConceptDecl(clang::ConceptDecl *d)
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
