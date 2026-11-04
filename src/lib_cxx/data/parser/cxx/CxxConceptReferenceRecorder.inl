// Inline implementations for CxxConceptReferenceRecorder.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/ASTConcept.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ExprConcepts.h>
#include <clang/Basic/Version.h>
#include <llvm/ADT/SmallVector.h>
#include <string>
#include "CanonicalFilePathCache.h"
#include "CxxAstVisitorComponentContext.h"
#include "CxxLocationExtractor.h"
#include "CxxSymbolRegistry.h"
#include "ParseLocation.h"
#include "ParserClient.h"
#include "ReferenceKind.h"
#include "clang_compat/ClangCompat.h"
#include "types.h"
#endif

inline CxxConceptReferenceRecorder::CxxConceptReferenceRecorder(
	clang::ASTContext& astContext,
	ParserClient& client,
	CanonicalFilePathCache& canonicalFilePathCache,
	CxxSymbolRegistry& symbols,
	CxxLocationExtractor& locations,
	CxxAstVisitorComponentContext& context)
	: m_astContext(astContext)
	, m_client(client)
	, m_canonicalFilePathCache(canonicalFilePathCache)
	, m_symbols(symbols)
	, m_locations(locations)
	, m_context(context)
{
}

inline void CxxConceptReferenceRecorder::recordTemplateParameterConceptReferences(
	const clang::TemplateDecl* templateDecl)
{
	if (const clang::TemplateParameterList* templateParameters = templateDecl->getTemplateParameters())
	{
		for (const clang::NamedDecl* namedDecl : *templateParameters)
		{
			if (const clang::TemplateTypeParmDecl* templateTypeParmDecl =
					clang::dyn_cast<clang::TemplateTypeParmDecl>(namedDecl))
			{
				if (const clang::TypeConstraint* typeConstraint =
						templateTypeParmDecl->getTypeConstraint())
				{
					recordConceptReference(typeConstraint);
				}
			}
		}
	}
}

inline void CxxConceptReferenceRecorder::recordNamedConceptReference(
	const clang::ConceptReference* conceptReference)
{
	if (const auto* conceptDecl = clang_compat::getNamedConceptDecl(conceptReference))
	{
		const Id conceptDeclId = m_symbols.getOrCreateSymbolId(conceptDecl);
		const Id contextSymbolId = m_symbols.getOrCreateSymbolId(m_context.getContext());
		const ParseLocation conceptNameLocation =
			m_locations.getParseLocation(conceptReference->getLocation());

		m_client.recordReference(
			ReferenceKind::USAGE, conceptDeclId, contextSymbolId, conceptNameLocation);
	}
}

inline void CxxConceptReferenceRecorder::recordSatisfiedConstraints(
	const clang::FunctionDecl* specialization, const clang::SourceLocation& useLocation)
{
	if (specialization == nullptr || specialization->isInvalidDecl())
	{
		return;
	}
	if (const clang::FunctionTemplateSpecializationInfo* info =
			specialization->getTemplateSpecializationInfo())
	{
		recordSatisfiedConstraints(
			info->getTemplate(),
			info->TemplateArguments->asArray(),
			recordedLocation(info->getPointOfInstantiation(), useLocation));
	}
}

inline void CxxConceptReferenceRecorder::recordSatisfiedConstraints(
	const clang::ClassTemplateSpecializationDecl* specialization, const clang::SourceLocation& useLocation)
{
	if (specialization == nullptr || specialization->isInvalidDecl())
	{
		return;
	}
	recordSatisfiedConstraints(
		specialization->getSpecializedTemplate(),
		specialization->getTemplateArgs().asArray(),
		recordedLocation(specialization->getPointOfInstantiation(), useLocation));
}

inline void CxxConceptReferenceRecorder::recordSatisfiedConstraints(
	const clang::VarTemplateSpecializationDecl* specialization, const clang::SourceLocation& useLocation)
{
	if (specialization == nullptr || specialization->isInvalidDecl())
	{
		return;
	}
	recordSatisfiedConstraints(
		specialization->getSpecializedTemplate(),
		specialization->getTemplateArgs().asArray(),
		recordedLocation(specialization->getPointOfInstantiation(), useLocation));
}

inline void CxxConceptReferenceRecorder::recordSatisfiedConstraints(
	const clang::TemplateSpecializationType* aliasSpecialization, const clang::SourceLocation& useLocation)
{
	if (aliasSpecialization == nullptr || aliasSpecialization->isDependentType())
	{
		return;
	}
	if (const auto* aliasTemplate = clang::dyn_cast_or_null<clang::TypeAliasTemplateDecl>(
			aliasSpecialization->getTemplateName().getAsTemplateDecl()))
	{
		recordSatisfiedConstraints(aliasTemplate, aliasSpecialization->template_arguments(), useLocation);
	}
}

inline void CxxConceptReferenceRecorder::recordSatisfiedConceptSpecialization(
	const clang::ConceptSpecializationExpr* conceptSpecialization)
{
	// Only a concept-id clang could evaluate: not one in a template that is not instantiated.
	if (conceptSpecialization == nullptr || conceptSpecialization->isValueDependent() ||
		conceptSpecialization->isInstantiationDependent() || !conceptSpecialization->isSatisfied())
	{
		return;
	}
	const llvm::ArrayRef<clang::TemplateArgument> arguments = conceptSpecialization->getTemplateArguments();
	if (!arguments.empty() && arguments.front().getKind() == clang::TemplateArgument::Type)
	{
		recordSatisfaction(
			arguments.front().getAsType(),
			conceptSpecialization->getNamedConcept(),
			conceptSpecialization->getConceptNameLoc());
	}
}

inline void CxxConceptReferenceRecorder::recordSatisfiedConstraints(
	const clang::TemplateDecl* templateDecl,
	llvm::ArrayRef<clang::TemplateArgument> arguments,
	const clang::SourceLocation& location)
{
	if (templateDecl == nullptr || templateDecl->getTemplateParameters() == nullptr)
	{
		return;
	}

	// Everything the template's declaration requires, in a conjunction: the type constraints of
	// its parameters (template <Shape T>, an abbreviated Shape auto), its requires-clause and a
	// function's trailing requires-clause. A constraint can only name the template's own
	// parameters at its own depth; a member template's references to the enclosing class
	// template's parameters are left out.
#if CLANG_VERSION_MAJOR >= 21
	llvm::SmallVector<clang::AssociatedConstraint, 4> constraints;
	templateDecl->getAssociatedConstraints(constraints);
	for (const clang::AssociatedConstraint& constraint: constraints)
	{
		recordSatisfiedConstraint(
			constraint.ConstraintExpr, templateDecl->getTemplateParameters()->getDepth(), arguments, location);
	}
#else
	llvm::SmallVector<const clang::Expr*, 4> constraints;
	templateDecl->getAssociatedConstraints(constraints);
	for (const clang::Expr* constraint: constraints)
	{
		recordSatisfiedConstraint(constraint, templateDecl->getTemplateParameters()->getDepth(), arguments, location);
	}
#endif
}

inline void CxxConceptReferenceRecorder::recordSatisfiedConstraint(
	const clang::Expr* constraint,
	const unsigned depth,
	llvm::ArrayRef<clang::TemplateArgument> arguments,
	const clang::SourceLocation& location)
{
	if (constraint == nullptr)
	{
		return;
	}
	constraint = constraint->IgnoreParenImpCasts();

	// A && B holds only if both do. Of A || B, or !A, nothing follows about either.
	if (const auto* binary = clang::dyn_cast<clang::BinaryOperator>(constraint))
	{
		if (binary->getOpcode() == clang::BO_LAnd)
		{
			recordSatisfiedConstraint(binary->getLHS(), depth, arguments, location);
			recordSatisfiedConstraint(binary->getRHS(), depth, arguments, location);
		}
		return;
	}

	// template <Shape... Ts>: (Shape<Ts> && ...), the concept holds for every element of the pack.
	if (const auto* fold = clang::dyn_cast<clang::CXXFoldExpr>(constraint))
	{
		const auto* pattern = fold->getOperator() == clang::BO_LAnd
			? clang::dyn_cast_or_null<clang::ConceptSpecializationExpr>(fold->getPattern()->IgnoreParenImpCasts())
			: nullptr;
		if (pattern == nullptr || pattern->getTemplateArguments().empty() ||
			pattern->getTemplateArguments().front().getKind() != clang::TemplateArgument::Type)
		{
			return;
		}
		const clang::QualType packType = pattern->getTemplateArguments().front().getAsType();
		const auto* parameter = packType->getAs<clang::TemplateTypeParmType>();
		if (parameter == nullptr || parameter->getDepth() != depth || parameter->getIndex() >= arguments.size() ||
			arguments[parameter->getIndex()].getKind() != clang::TemplateArgument::Pack)
		{
			return;
		}
		for (const clang::TemplateArgument& element: arguments[parameter->getIndex()].pack_elements())
		{
			if (element.getKind() == clang::TemplateArgument::Type)
			{
				recordSatisfaction(element.getAsType(), pattern->getNamedConcept(), location);
			}
		}
		return;
	}

	// Shape<T>, std::convertible_to<T, int>: the concept's first argument is the type that
	// satisfies it, the others are arguments of the concept. Only a first argument that is one of
	// the template's parameters is matched to the specialization's argument; Shape<T*> or
	// Shape<typename T::value_type> would take substituting, and are left out.
	const auto* conceptId = clang::dyn_cast<clang::ConceptSpecializationExpr>(constraint);
	if (conceptId == nullptr || conceptId->getTemplateArguments().empty() ||
		conceptId->getTemplateArguments().front().getKind() != clang::TemplateArgument::Type)
	{
		return;
	}
	const clang::QualType constrainedType = conceptId->getTemplateArguments().front().getAsType();
	const auto* parameter = constrainedType->getAs<clang::TemplateTypeParmType>();
	if (parameter == nullptr || parameter->getDepth() != depth || parameter->getIndex() >= arguments.size())
	{
		return;
	}
	const clang::TemplateArgument& argument = arguments[parameter->getIndex()];
	if (argument.getKind() == clang::TemplateArgument::Type)
	{
		recordSatisfaction(argument.getAsType(), conceptId->getNamedConcept(), location);
	}
}

inline void CxxConceptReferenceRecorder::recordSatisfaction(
	clang::QualType type, const clang::ConceptDecl* conceptDecl, const clang::SourceLocation& location)
{
	if (conceptDecl == nullptr || type.isNull() || type->isDependentType() || location.isInvalid())
	{
		return;
	}

	// Shape<Circle&> or Shape<const Circle> (a forwarding reference deduces Circle&): the node
	// is Circle's. A pointer, an array, a function type is a type of its own with no node, and is
	// left out rather than drawn as the class it points to.
	type = type.getNonReferenceType().getUnqualifiedType().getCanonicalType();

	Id typeId = 0;
	bool typeInProjectFile = false;
	if (const clang::TagDecl* tagDecl = type->getAsTagDecl())
	{
		typeId = m_symbols.getOrCreateSymbolId(tagDecl);
		typeInProjectFile = isInProjectFile(tagDecl);
	}
	else if (type->isBuiltinType())
	{
		typeId = m_symbols.getOrCreateSymbolId(type.getTypePtr());
		m_client.recordSymbolKind(typeId, SymbolKind::BUILTIN_TYPE);
		m_client.recordDefinitionKind(typeId, DefinitionKind::EXPLICIT);
	}
	if (typeId == 0)
	{
		return;
	}

	// The project's own facts: its types satisfying any concept, and any type satisfying its
	// concepts. A standard type meeting a standard concept (int, std::integral) is the library's
	// business, and would be most of the edges. Of a library's concepts, its implementation
	// details (a reserved name: stdexec::__movable_value) and a lambda meeting one (every
	// predicate handed to std::ranges::find_if is an std::indirect_unary_predicate) are the
	// library checking its own arguments, not something the project says about its types.
	const bool conceptInProjectFile = isInProjectFile(conceptDecl);
	if (!typeInProjectFile && !conceptInProjectFile)
	{
		return;
	}
	if (!conceptInProjectFile)
	{
		const std::string conceptName = conceptDecl->getNameAsString();
		const bool reserved = conceptName.size() >= 2 && conceptName[0] == '_' &&
			(conceptName[1] == '_' || (conceptName[1] >= 'A' && conceptName[1] <= 'Z'));
		const auto* record = clang::dyn_cast_or_null<clang::CXXRecordDecl>(type->getAsTagDecl());
		if (reserved || (record != nullptr && record->isLambda()))
		{
			return;
		}
	}

	const Id conceptId = m_symbols.getOrCreateSymbolId(conceptDecl);
	m_client.recordSymbolKind(conceptId, SymbolKind::CONCEPT);
	m_client.recordReference(ReferenceKind::SATISFIES, conceptId, typeId, m_locations.getParseLocation(location));
}

inline bool CxxConceptReferenceRecorder::isInProjectFile(const clang::Decl* decl)
{
	const clang::SourceManager& sourceManager = m_astContext.getSourceManager();
	const clang::SourceLocation location = sourceManager.getExpansionLoc(decl->getLocation());
	return location.isValid() &&
		m_canonicalFilePathCache.isProjectFile(sourceManager.getFileID(location), sourceManager);
}

inline clang::SourceLocation CxxConceptReferenceRecorder::recordedLocation(
	const clang::SourceLocation& pointOfInstantiation, const clang::SourceLocation& useLocation)
{
	// One location per specialization -- where it was first needed -- rather than every use of
	// it, when that is the project's code. The use is in a project file: the visitors only call in
	// for those.
	if (pointOfInstantiation.isValid())
	{
		const clang::SourceManager& sourceManager = m_astContext.getSourceManager();
		const clang::SourceLocation location = sourceManager.getExpansionLoc(pointOfInstantiation);
		if (location.isValid() &&
			m_canonicalFilePathCache.isProjectFile(sourceManager.getFileID(location), sourceManager))
		{
			return pointOfInstantiation;
		}
	}
	return useLocation;
}
