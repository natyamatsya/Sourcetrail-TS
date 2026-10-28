// Inline implementations for CxxDeclNameResolver.h. Included via CxxNameResolverBodies.h (classic)
// or the srctrl.cxx:parser wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/ASTContext.h>

#include "CanonicalFilePathCache.h"
#include "CxxFunctionDeclName.h"
#include "CxxStaticFunctionDeclName.h"
#include "CxxVariableDeclName.h"
#include "ScopedSwitcher.h"
#include "utilityClang.h"
#endif

inline CxxDeclNameResolver::CxxDeclNameResolver(CanonicalFilePathCache* canonicalFilePathCache)
	: CxxNameResolver(canonicalFilePathCache), m_currentDecl(nullptr)
{
}

inline CxxDeclNameResolver::CxxDeclNameResolver(const CxxNameResolver* other)
	: CxxNameResolver(other), m_currentDecl(nullptr)
{
}

inline CxxName CxxDeclNameResolver::getName(const clang::NamedDecl* declaration)
{
	declaration = utility::getFirstDecl(declaration);

	// A using-declaration is a use of what it brings into scope, so it is named by that: by the
	// name it is spelled with (`std::string_view` for std::__1::string_view, `Ts::operator()`) it
	// would be a node of its own that no declaration has.
	if (const clang::UsingDecl* usingDecl = clang::dyn_cast_or_null<clang::UsingDecl>(declaration))
	{
		if (usingDecl->shadow_size() > 0)
		{
			const clang::UsingShadowDecl* shadowDecl = *usingDecl->shadow_begin();
			// `using Base::Base;` inherits all of Base's constructors: it uses the base class.
			if (const clang::ConstructorUsingShadowDecl* constructorShadowDecl =
					clang::dyn_cast<clang::ConstructorUsingShadowDecl>(shadowDecl))
			{
				if (const clang::CXXRecordDecl* baseDecl = constructorShadowDecl->getNominatedBaseClass())
				{
					return getName(baseDecl);
				}
			}
			// An overload set has no node of its own; it keeps the name as spelled.
			else if (usingDecl->shadow_size() == 1)
			{
				return getName(shadowDecl->getTargetDecl()->getUnderlyingDecl());
			}
		}
	}

	if ((declaration) && (clang::isa<clang::CXXRecordDecl>(declaration)) &&
		(clang::dyn_cast<clang::CXXRecordDecl>(declaration)->isLambda()))
	{
		// avoid triggering assert
		clang::DeclarationName Name = clang::dyn_cast<clang::CXXRecordDecl>(declaration)
										  ->getASTContext()
										  .DeclarationNames.getCXXOperatorName(clang::OO_Call);
		clang::DeclContext::lookup_result Calls =
			clang::dyn_cast<clang::CXXRecordDecl>(declaration)->lookup(Name);
		if (Calls.empty())
		{
			return CxxName::make<CxxDeclName>("unsolved-lambda");
		}
		else
		{
			declaration = clang::dyn_cast<clang::CXXRecordDecl>(declaration)->getLambdaCallOperator();
		}
	}

	if (!declaration)
	{
		return {};
	}

	CxxName declName = getDeclName(clang::dyn_cast<const clang::NamedDecl>(declaration));
	if (declName)
	{
		if (const clang::UsingDecl* usingDecl = clang::dyn_cast_or_null<clang::UsingDecl>(declaration))
		{
			declName.setParent(CxxSpecifierNameResolver(this).getName(usingDecl->getQualifier()));
		}
		else if (
			!clang::isa<clang::TemplateTypeParmDecl>(declaration) &&
			!clang::isa<clang::NonTypeTemplateParmDecl>(declaration) &&
			!clang::isa<clang::TemplateTemplateParmDecl>(declaration))
		{
			declName.setParent(getContextName(declaration->getDeclContext()));
		}
	}

	return declName;
}

inline CxxName CxxDeclNameResolver::getContextName(const clang::DeclContext* declContext)
{
	CxxName contextDeclName;

	if (declContext && !ignoresContext(declContext))
	{
		if (const clang::NamedDecl* contextNamedDecl = clang::dyn_cast_or_null<clang::NamedDecl>(
				declContext))
		{
			contextDeclName = getDeclName(contextNamedDecl);
			if (contextDeclName)
			{
				contextDeclName.setParent(getContextName(declContext->getParent()));
			}
			else
			{
				contextDeclName = getContextName(declContext->getParent());
			}
		}
	}
	return contextDeclName;
}

inline CxxName CxxDeclNameResolver::getDeclName(const clang::NamedDecl* declaration)
{
	if (declaration)
	{
		[[maybe_unused]]
		ScopedSwitcher<const clang::NamedDecl*> switcher(m_currentDecl, declaration);

		// TODO (petermost): Why does this return "variableName:auto" (e.g.: 'x:auto') for abbreviated template function parameters?

		std::string declNameString = declaration->getNameAsString();

		if (const clang::TagDecl* tagDecl = clang::dyn_cast_or_null<clang::TagDecl>(declaration))
		{
			if (const clang::TypedefNameDecl* typedefNameDecl = tagDecl->getTypedefNameForAnonDecl())
			{
				declNameString = typedefNameDecl->getNameAsString();
			}
		}

		if (const clang::TypeAliasDecl* typeAliasDecl =
				clang::dyn_cast_or_null<clang::TypeAliasDecl>(declaration))
		{
			clang::TypeAliasTemplateDecl* templatedDeclaration =
				typeAliasDecl->getDescribedAliasTemplate();
			if (templatedDeclaration)
			{
				return getDeclName(templatedDeclaration);
			}
		}

		if (const clang::RecordDecl* recordDecl = clang::dyn_cast_or_null<clang::RecordDecl>(
				declaration))
		{
			if (recordDecl->isLambda())
			{
				// we skip this node because its child (the lambda call operator) has already been recorded.
				return {};
			}
			else if (declNameString.empty())
			{
				std::string symbolKindName = "class";
				if (recordDecl->isStruct())
				{
					symbolKindName = "struct";
				}
				else if (recordDecl->isUnion())
				{
					symbolKindName = "union";
				}

				return CxxName::make<CxxDeclName>(
					getNameForAnonymousSymbol(symbolKindName, declaration));
			}
			else if (const clang::CXXRecordDecl* cxxRecordDecl = clang::dyn_cast_or_null<clang::CXXRecordDecl>(declaration))
			{
				if (clang::ClassTemplateDecl* templateClassDeclaration =
						cxxRecordDecl->getDescribedClassTemplate())
				{
					return getDeclName(templateClassDeclaration);
				}
				else if (clang::isa<clang::ClassTemplatePartialSpecializationDecl>(declaration))
				{
					return CxxName::make<CxxDeclName>(
						std::move(declNameString),
						getTemplateParameterStringsOfPartialSpecialization(
							clang::dyn_cast<clang::ClassTemplatePartialSpecializationDecl>(
								declaration)));
				}
				else if (
					const clang::ClassTemplateSpecializationDecl* templateSpecialitarionDecl =
						clang::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(declaration))
				{
					std::vector<std::string> templateArguments;
					const clang::TemplateArgumentList& templateArgumentList =
						templateSpecialitarionDecl->getTemplateArgs();
					for (unsigned i = 0; i < templateArgumentList.size(); i++)
					{
						if (templateArgumentList.get(i).isDependent())
						{
							llvm::PointerUnion<clang::ClassTemplateDecl*, clang::ClassTemplatePartialSpecializationDecl*>
								pu = templateSpecialitarionDecl->getSpecializedTemplateOrPartial();
							if (clang::isa<clang::ClassTemplateDecl*>(pu))
							{
								return getDeclName(clang::cast<clang::ClassTemplateDecl*>(pu));
							}
							else if (clang::isa<clang::ClassTemplatePartialSpecializationDecl*>(pu))
							{
								return getDeclName(clang::cast<clang::ClassTemplatePartialSpecializationDecl*>(pu));
							}
						}

						templateArguments.push_back(getTemplateArgumentName(templateArgumentList.get(i)));
					}
					return CxxName::make<CxxDeclName>(std::move(declNameString), std::move(templateArguments));
				}
			}
		}
		else if (const clang::FunctionDecl* functionDecl = clang::dyn_cast<clang::FunctionDecl>(declaration))
		{
			std::string functionName = declNameString;
			std::vector<std::string> templateArguments;

			if ((clang::dyn_cast_or_null<clang::CXXMethodDecl>(functionDecl)) &&
				(clang::dyn_cast_or_null<clang::CXXMethodDecl>(functionDecl)->getParent()->isLambda()))
			{
				const clang::SourceManager& sourceManager =
					declaration->getASTContext().getSourceManager();
				const clang::PresumedLoc& presumedBegin = sourceManager.getPresumedLoc(
					clang::dyn_cast_or_null<clang::CXXMethodDecl>(functionDecl)
						->getParent()
						->getBeginLoc());
				functionName = "lambda at " + std::to_string(presumedBegin.getLine()) + ":" +
					std::to_string(presumedBegin.getColumn());
			}
			else if (clang::FunctionTemplateDecl* templateFunctionDeclaration = functionDecl->getDescribedFunctionTemplate())
			{
				templateArguments = getTemplateParameterStrings(templateFunctionDeclaration);
			}
			else if (functionDecl->isFunctionTemplateSpecialization())
			{
				if (const clang::TemplateArgumentList* templateArgumentList =
					functionDecl->getTemplateSpecializationArgs())
				{
					for (unsigned i = 0; i < templateArgumentList->size(); i++)
					{
						const clang::TemplateArgument& templateArgument = templateArgumentList->get(i);
						if (templateArgument.isDependent())
						{
							if (clang::FunctionTemplateDecl* templateFunctionDeclaration =
									functionDecl->getPrimaryTemplate())
							{
								return getDeclName(templateFunctionDeclaration);
							}
						}
						templateArguments.push_back(getTemplateArgumentName(templateArgument));
					}
				}
			}

			// A constructor or destructor is named after its class. Clang's name for it is the
			// class's injected name, which in a class template carries the template's parameters
			// (`static_str<N>`), and a constructor inherited by `using Base::Base;` has the base
			// class's name.
			if (clang::isa<clang::CXXConstructorDecl, clang::CXXDestructorDecl>(functionDecl))
			{
				const clang::CXXRecordDecl* recordDecl =
					clang::cast<clang::CXXMethodDecl>(functionDecl)->getParent();
				if (!recordDecl->isLambda() && recordDecl->getIdentifier())
				{
					functionName = (clang::isa<clang::CXXDestructorDecl>(functionDecl) ? "~" : "") +
						recordDecl->getName().str();
				}
			}

			bool isStatic = false;
			CxxQualifierFlags methodQualifiers;

			// The name is the method's identity, so it carries what tells overloads apart -- the
			// cv- and ref-qualifiers -- and nothing else. constexpr is not part of the signature,
			// and a defaulted or implicit member is constexpr in one -std and not in another, so
			// with it the same member would become one node per language standard.
			if (const clang::CXXMethodDecl* methodDecl = clang::dyn_cast<const clang::CXXMethodDecl>(declaration))
			{
				isStatic = methodDecl->isStatic();
				if (methodDecl->isConst())
					methodQualifiers.addQualifier(CxxQualifierFlags::QualifierType::CONST);
				if (methodDecl->isVolatile())
					methodQualifiers.addQualifier(CxxQualifierFlags::QualifierType::VOLATILE);
				switch (methodDecl->getRefQualifier())
				{
				case clang::RQ_LValue:
					methodQualifiers.addQualifier(CxxQualifierFlags::QualifierType::LVALUE_REFERENCE);
					break;
				case clang::RQ_RValue:
					methodQualifiers.addQualifier(CxxQualifierFlags::QualifierType::RVALUE_REFERENCE);
					break;
				case clang::RQ_None:
					break;
				}
			}
			else
				isStatic = functionDecl->getStorageClass() == clang::SC_Static;

			CxxTypeNameResolver typeNameResolver(this);
			typeNameResolver.ignoreContextDecl(functionDecl);
			typeNameResolver.spellDependentSpecializations();
			std::unique_ptr<CxxTypeName> returnTypeName = CxxTypeName::makeUnsolvedIfNull(
				typeNameResolver.getName(functionDecl->getReturnType()));

			// An instantiation's function parameter pack is one parameter per element, named like
			// its template arguments: f<int, char>(int, char).
			std::vector<std::unique_ptr<CxxTypeName>> parameterTypeNames;
			for (unsigned int i = 0; i < functionDecl->param_size(); i++)
			{
				parameterTypeNames.push_back(CxxTypeName::makeUnsolvedIfNull(
					typeNameResolver.getName(functionDecl->parameters()[i]->getType())));
			}

			// A static function is one per file that defines it: named by that file, a header's
			// static (inline) function is one node however many TUs include it, while same-named
			// statics of two .c files stay apart.
			if (!clang::isa<clang::CXXMethodDecl>(declaration) && isStatic)
			{
				return CxxName::make<CxxStaticFunctionDeclName>(
					std::move(functionName),
					std::move(templateArguments),
					std::move(returnTypeName),
					std::move(parameterTypeNames),
					getCanonicalFilePathCache()->getDeclarationFileName(declaration));
			}

			return CxxName::make<CxxFunctionDeclName>(
				std::move(functionName),
				std::move(templateArguments),
				std::move(returnTypeName),
				std::move(parameterTypeNames),
				methodQualifiers,
				isStatic);
		}
		else if (const clang::FunctionTemplateDecl *functionTemplateDecl = clang::dyn_cast<clang::FunctionTemplateDecl>(declaration))
		{
			return getDeclName(functionTemplateDecl->getTemplatedDecl());
		}
		else if (const clang::FieldDecl *fieldDecl = clang::dyn_cast<clang::FieldDecl>(declaration))
		{
			CxxTypeNameResolver typenNameResolver(this);
			typenNameResolver.ignoreContextDecl(fieldDecl);
			std::unique_ptr<CxxTypeName> typeName = CxxTypeName::makeUnsolvedIfNull(typenNameResolver.getName(fieldDecl->getType()));
			return CxxName::make<CxxVariableDeclName>(std::move(declNameString), std::vector<std::string>(), std::move(typeName), false);
		}
		else if (clang::isa<clang::NamespaceDecl>(declaration) && clang::dyn_cast<clang::NamespaceDecl>(declaration)->isAnonymousNamespace())
		{
#if LLVM_VERSION_MAJOR >= 19
			declaration = clang::dyn_cast<clang::NamespaceDecl>(declaration)->getFirstDecl();
#else
			declaration = clang::dyn_cast<clang::NamespaceDecl>(declaration)->getOriginalNamespace();
#endif
			return CxxName::make<CxxDeclName>(getNameForAnonymousSymbol("namespace", declaration));
		}
		else if (clang::isa<clang::EnumDecl>(declaration) && declNameString.empty())
		{
			return CxxName::make<CxxDeclName>(getNameForAnonymousSymbol("enum", declaration));
		}
		else if (
			(clang::isa<clang::TemplateTypeParmDecl>(declaration) ||
			 clang::isa<clang::NonTypeTemplateParmDecl>(declaration) ||
			 clang::isa<clang::TemplateTemplateParmDecl>(declaration)) &&
			declNameString.empty())
		{
			return CxxName::make<CxxDeclName>(
				getNameForAnonymousSymbol("template parameter", declaration));
		}
		else if (clang::isa<clang::ParmVarDecl>(declaration) && declNameString.empty())
		{
			return CxxName::make<CxxDeclName>(getNameForAnonymousSymbol("parameter", declaration));
		}
		else if (const clang::VarDecl *varDecl = clang::dyn_cast<clang::VarDecl>(declaration))
		{
			if (varDecl->getParentFunctionOrMethod() == nullptr)
			{
				bool isStatic = false;
				if (varDecl->getAccess() != clang::AS_none)
				{
					// var is declared inside a type and must be static (non-statics are stored as
					// clang::FieldDecl)
					isStatic = true;
				}
				else
				{
					// nothing todo, varDecl is global (and non-static)
				}

				CxxTypeNameResolver typenNameResolver(this);
				typenNameResolver.ignoreContextDecl(varDecl);
				std::unique_ptr<CxxTypeName> typeName = CxxTypeName::makeUnsolvedIfNull(typenNameResolver.getName(varDecl->getType(), varDecl));

				std::string varName = declNameString;
				if (utility::getSymbolKind(varDecl) == SymbolKind::GLOBAL_VARIABLE && varDecl->getStorageClass() == clang::SC_Static)
				{
					// A static global variable is only visible in its translation unit, so
					// same-named ones of different files are different variables: we add the name
					// of the (maybe header) file that declares it. A header's static variable is one
					// node for all TUs that include it -- one declaration in the source -- as is a
					// header's static function.
					const std::string scopeFileName =
						getCanonicalFilePathCache()->getDeclarationFileName(declaration);

					if (!scopeFileName.empty())
					{
						varName = declNameString + " (" + scopeFileName + ')';
					}
				}

				std::vector<std::string> templateParameterNames;
				if (varDecl->getDescribedVarTemplate())
				{
					const clang::VarTemplateDecl* templateDeclaration =
						varDecl->getDescribedVarTemplate();
					templateParameterNames = getTemplateParameterStrings(templateDeclaration);
				}
				else if (clang::isa<clang::VarTemplatePartialSpecializationDecl>(declaration))
				{
					templateParameterNames = getTemplateParameterStringsOfPartialSpecialization(
						clang::dyn_cast<clang::VarTemplatePartialSpecializationDecl>(declaration));
				}
				else if (clang::isa<clang::VarTemplateSpecializationDecl>(declaration))
				{
					const clang::VarTemplateSpecializationDecl* templateSpecializationDeclaration =
						clang::dyn_cast_or_null<clang::VarTemplateSpecializationDecl>(varDecl);
					const clang::TemplateArgumentList& templateArgumentList =
						templateSpecializationDeclaration->getTemplateArgs();
					for (unsigned i = 0; i < templateArgumentList.size(); i++)
					{
						const clang::TemplateArgument& templateArgument = templateArgumentList.get(i);
						if (templateArgument.isDependent())
						{
							llvm::PointerUnion<clang::VarTemplateDecl*, clang::VarTemplatePartialSpecializationDecl*>
								pu = templateSpecializationDeclaration->getSpecializedTemplateOrPartial();
							if (clang::isa<clang::VarTemplateDecl*>(pu))
							{
								return getDeclName(clang::cast<clang::VarTemplateDecl*>(pu));
							}
							else if (clang::isa<clang::VarTemplatePartialSpecializationDecl*>(pu))
							{
								return getDeclName(clang::cast<clang::VarTemplatePartialSpecializationDecl*>(pu));
							}
						}
						templateParameterNames.push_back(getTemplateArgumentName(templateArgument));
					}
				}

				return CxxName::make<CxxVariableDeclName>(
					std::move(varName),
					std::move(templateParameterNames),
					std::move(typeName),
					isStatic);
			}
		}
		else if (clang::isa<clang::VarTemplateDecl>(declaration))
		{
			const clang::VarTemplateDecl* varTemplateDecl = clang::dyn_cast<clang::VarTemplateDecl>(
				declaration);
			return getDeclName(varTemplateDecl->getTemplatedDecl());
		}
		else if (clang::isa<clang::TemplateDecl>(
					 declaration))	  // also triggers on TemplateTemplateParmDecl
		{
			return CxxName::make<CxxDeclName>(
				std::move(declNameString),
				getTemplateParameterStrings(clang::dyn_cast<clang::TemplateDecl>(declaration)));
		}

		if (!declNameString.empty())
		{
			return CxxName::make<CxxDeclName>(std::move(declNameString));
		}
	}

	// LOG_ERROR("could not resolve name of decl at: " +
	// declaration->getLocation().printToString(sourceManager));
	return CxxName::make<CxxDeclName>(getNameForAnonymousSymbol("symbol", declaration));
}

inline std::string CxxDeclNameResolver::getNameForAnonymousSymbol(
	const std::string& symbolKindName, const clang::Decl* declaration)
{
	const clang::SourceManager& sourceManager = declaration->getASTContext().getSourceManager();
	const clang::PresumedLoc& presumedBegin = sourceManager.getPresumedLoc(declaration->getBeginLoc());

	if (presumedBegin.isValid())
	{
		return "anonymous " + symbolKindName + " (" +
			getCanonicalFilePathCache()->getDeclarationFileName(declaration) + '<' +
			std::to_string(presumedBegin.getLine()) + ':' +
			std::to_string(presumedBegin.getColumn()) + ">)";
	}
	return "anonymous " + symbolKindName;
}

inline std::vector<std::string> CxxDeclNameResolver::getTemplateParameterStrings(
	const clang::TemplateDecl* templateDecl)
{
	std::vector<std::string> templateParameterStrings;
	clang::TemplateParameterList* parameterList = templateDecl->getTemplateParameters();
	for (unsigned i = 0; i < parameterList->size(); i++)
	{
		templateParameterStrings.push_back(getTemplateParameterString(parameterList->getParam(i)));
	}
	return templateParameterStrings;
}

inline std::string CxxDeclNameResolver::getTemplateParameterString(const clang::NamedDecl* parameter)
{
	CxxTemplateParameterStringResolver parameterStringResolver(this);
	if (clang::isa<clang::TemplateDecl>(m_currentDecl) &&
		clang::dyn_cast<clang::TemplateDecl>(m_currentDecl)->getTemplatedDecl())
	{
		parameterStringResolver.ignoreContextDecl(
			clang::dyn_cast<clang::TemplateDecl>(m_currentDecl)->getTemplatedDecl());
	}
	else	// works for partial template specializations
	{
		parameterStringResolver.ignoreContextDecl(m_currentDecl);
	}
	parameterStringResolver.ignoreContextDecl(parameter);

	return parameterStringResolver.getTemplateParameterString(parameter);
}

inline std::string CxxDeclNameResolver::getTemplateArgumentName(const clang::TemplateArgument& argument)
{
	return CxxTemplateArgumentNameResolver(this).getTemplateArgumentName(argument);
}

inline std::string CxxDeclNameResolver::getWrittenTemplateArgumentName(
	const clang::TemplateArgumentLoc& argument)
{
	return CxxTemplateArgumentNameResolver(this).getWrittenTemplateArgumentName(argument);
}
