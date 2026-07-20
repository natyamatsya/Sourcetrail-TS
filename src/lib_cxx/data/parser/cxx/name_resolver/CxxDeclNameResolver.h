#ifndef CXX_DECL_NAME_RESOLVER_H
#define CXX_DECL_NAME_RESOLVER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/DeclTemplate.h>

#include "CxxDeclName.h"
#include "CxxNameResolver.h"
#include "CxxTypeName.h"
#include "CxxTypeNameResolver.h"
#endif

class CanonicalFilePathCache;

SRCTRL_EXPORT class CxxDeclNameResolver: public CxxNameResolver
{
public:
	CxxDeclNameResolver(CanonicalFilePathCache* canonicalFilePathCache);
	CxxDeclNameResolver(const CxxNameResolver* other);

	CxxName getName(const clang::NamedDecl* declaration);

private:
	CxxName getContextName(const clang::DeclContext* declaration);
	CxxName getDeclName(const clang::NamedDecl* declaration);
	std::string getTranslationUnitMainFileName(const clang::Decl* declaration);
	std::string getNameForAnonymousSymbol(
		const std::string& symbolKindName, const clang::Decl* declaration);
	std::vector<std::string> getTemplateParameterStrings(const clang::TemplateDecl* templateDecl);
	template <typename T>
	std::vector<std::string> getTemplateParameterStringsOfPartialSpecialization(const T* templateDecl);
	std::string getTemplateParameterString(const clang::NamedDecl* parameter);
	std::string getTemplateArgumentName(const clang::TemplateArgument& argument);

	const clang::NamedDecl* m_currentDecl;
};


template <typename T>
std::vector<std::string> CxxDeclNameResolver::getTemplateParameterStringsOfPartialSpecialization(
	const T* partialSpecializationDecl)
{
	std::vector<std::string> templateParameterNames;
	clang::TemplateParameterList* parameterList = partialSpecializationDecl->getTemplateParameters();

	const clang::TemplateArgumentList& templateArgumentList =
		partialSpecializationDecl->getTemplateArgs();
	for (unsigned i = 0; i < templateArgumentList.size(); i++)
	{
		const clang::TemplateArgument& templateArgument = templateArgumentList.get(i);
		const clang::TemplateArgument::ArgKind argKind = templateArgument.getKind();
		if (templateArgument.isDependent())
		{
			if (argKind == clang::TemplateArgument::Type && !templateArgument.getAsType().isNull())
			{
				const clang::Type* argumentType = templateArgument.getAsType().getTypePtr();
				if (const clang::TemplateTypeParmType* ttpt =
						clang::dyn_cast<clang::TemplateTypeParmType>(argumentType))
				{
					if (ttpt->getDepth() == parameterList->getDepth())
					{
						templateParameterNames.push_back(
							getTemplateParameterString(parameterList->getParam(ttpt->getIndex())));
					}
					else
					{
						// TODO: fix case when arg depends on template parameter of outer template
						// class, or depends on first template parameter.
						templateParameterNames.push_back(
							"arg" + std::to_string(ttpt->getDepth()) + "_" +
							std::to_string(ttpt->getIndex()));
					}
				}
				else
				{
					templateParameterNames.push_back(
						CxxTypeName::makeUnsolvedIfNull(CxxTypeNameResolver(this).getName(argumentType))
							->toString());
				}
			}
			else if (
				argKind == clang::TemplateArgument::Template &&
				!templateArgument.getAsTemplate().isNull())
			{
				const clang::TemplateTemplateParmDecl* decl =
					clang::dyn_cast<clang::TemplateTemplateParmDecl>(
						templateArgument.getAsTemplate().getAsTemplateDecl());
				if (decl)
				{
					if (decl->getDepth() == parameterList->getDepth())
					{
						templateParameterNames.push_back(
							getTemplateParameterString(parameterList->getParam(decl->getIndex())));
					}
					else
					{
						// TODO: fix case when arg depends on template parameter of outer template
						// class, or depends on first template parameter.
						templateParameterNames.push_back(
							"arg" + std::to_string(decl->getDepth()) + "_" +
							std::to_string(decl->getIndex()));
					}
				}
				else
				{
					templateParameterNames.push_back(getTemplateArgumentName(templateArgument));
				}
			}
			else
			{
				templateParameterNames.push_back(getTemplateArgumentName(templateArgument));
			}
		}
		else
		{
			templateParameterNames.push_back(getTemplateArgumentName(templateArgument));
		}
	}
	return templateParameterNames;
}

// Classic build: the resolver family is mutually recursive, so the inline bodies parse in ONE
// place, once every class is complete. This header is the family's apex (its class-definition
// includes pull the deepest chain); its bottom completes the remaining siblings and then includes
// all bodies. The other headers bottom-include THIS header, so any entry point converges here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxNameResolverBodies.h"
#endif

#endif	  // CXX_DECL_NAME_RESOLVER_H
