#include "CxxTypeNameResolver.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/PrettyPrinter.h>

#include "CxxDeclNameResolver.h"
#include "CxxSpecifierNameResolver.h"
#include "CxxTemplateArgumentNameResolver.h"
#include "logging.h"
#include "utilityClang.h"

using namespace std;
using namespace clang;
using namespace utility;

CxxTypeNameResolver::CxxTypeNameResolver(CanonicalFilePathCache* canonicalFilePathCache)
	: CxxNameResolver(canonicalFilePathCache)
{
}

CxxTypeNameResolver::CxxTypeNameResolver(const CxxNameResolver* other): CxxNameResolver(other) {}

std::unique_ptr<CxxTypeName> CxxTypeNameResolver::getName(const clang::QualType& qualType, const VarDecl *varDecl)
{
	std::unique_ptr<CxxTypeName> typeName = getName(qualType.getTypePtr());
	if (typeName)
	{
		if (varDecl != nullptr && varDecl->isConstexpr())
			typeName->addQualifier(CxxQualifierFlags::QualifierType::CONSTEXPR);
		else if (qualType.isConstQualified())
			typeName->addQualifier(CxxQualifierFlags::QualifierType::CONST);
	}
	return typeName;
}

std::unique_ptr<CxxTypeName> CxxTypeNameResolver::getName(const clang::Type* type)
{
	if (type)
	{
		const auto resolveDeclName = [this](auto* d) { return CxxDeclNameResolver(this).getName(d); };

		switch (type->getTypeClass())
		{
		case clang::Type::Paren:
		{
			return getName(type->getAs<clang::ParenType>()->getInnerType());
		}
		case clang::Type::Attributed:
		{
			return getName(type->getAs<clang::AttributedType>()->getModifiedType());
		}
		case clang::Type::InjectedClassName:
		{
			CxxName declNameErased = resolveDeclName(
				type->getAs<clang::InjectedClassNameType>()->getDecl());
			if (const CxxDeclName* declName = declNameErased.target<CxxDeclName>())
			{
				return std::make_unique<CxxTypeName>(
					declName->getName(),
					declName->getTemplateParameterNames(),
					declName->getParent());
			}
			break;
		}
		case clang::Type::Typedef:
		{
			CxxName declNameErased = resolveDeclName(
				type->getAs<clang::TypedefType>()->getDecl());
			if (const CxxDeclName* declName = declNameErased.target<CxxDeclName>())
			{
				return std::make_unique<CxxTypeName>(
					declName->getName(), std::vector<std::string>(), declName->getParent());
			}
			break;
		}
		case clang::Type::MemberPointer:
		case clang::Type::Pointer:
		{
			std::unique_ptr<CxxTypeName> typeName = getName(type->getPointeeType());
			if (typeName)
			{
				typeName->addModifier(CxxTypeName::Modifier("*"));
			}
			return typeName;
		}
		case clang::Type::ConstantArray:
		case clang::Type::DependentSizedArray:
		case clang::Type::IncompleteArray:
		case clang::Type::VariableArray:
		{
			std::unique_ptr<CxxTypeName> typeName = getName(
				clang::dyn_cast<clang::ArrayType>(type)->getElementType());
			if (typeName)
			{
				typeName->addModifier(CxxTypeName::Modifier("[]"));
			}
			return typeName;
		}
		case clang::Type::LValueReference:
		{
			std::unique_ptr<CxxTypeName> typeName = getName(type->getPointeeType());
			if (typeName)
			{
				typeName->addModifier(CxxTypeName::Modifier("&"));
			}
			return typeName;
		}
		case clang::Type::RValueReference:
		{
			std::unique_ptr<CxxTypeName> typeName = getName(type->getPointeeType());
			if (typeName)
			{
				typeName->addModifier(CxxTypeName::Modifier("&&"));
			}
			return typeName;
		}
#if LLVM_VERSION_MAJOR < 22
		case clang::Type::Elaborated:
			return getName(clang::dyn_cast<clang::ElaboratedType>(type)->getNamedType());
#endif
		case clang::Type::Enum:
		case clang::Type::Record:
		{
			CxxName declNameErased = resolveDeclName(
				type->getAs<clang::TagType>()->getDecl());
			if (const CxxDeclName* declName = declNameErased.target<CxxDeclName>())
			{
				return std::make_unique<CxxTypeName>(
					declName->getName(),
					declName->getTemplateParameterNames(),	  // contains template arguments if decl
															  // is a template specialization
					declName->getParent());
			}
			break;
		}
		case clang::Type::Builtin:
		{
			clang::PrintingPolicy pp = makePrintingPolicyForCPlusPlus();

			return std::make_unique<CxxTypeName>(
				type->getAs<clang::BuiltinType>()->getName(pp).str(),
				std::vector<std::string>());
		}
		case clang::Type::TemplateSpecialization:
		{
			const clang::TagType* tagType =
				type->getAs<clang::TagType>();	  // remove this case when NameHierarchy is split
												  // into namepart and parameter part
			if (tagType)
			{
				CxxName declNameErased = resolveDeclName(
					tagType->getDecl());
				if (const CxxDeclName* declName = declNameErased.target<CxxDeclName>())
				{
					return std::make_unique<CxxTypeName>(
						declName->getName(),
						declName->getTemplateParameterNames(),
						declName->getParent());
				}
			}
			else	// specialization of a template template parameter (no concrete class)
					// important, may help: has no underlying decl!
			{
				const clang::TemplateSpecializationType* templateSpecializationType =
					type->getAs<clang::TemplateSpecializationType>();
				const clang::TemplateName templateName =
					templateSpecializationType->getTemplateName();
				CxxName declNameErased =
					resolveDeclName(templateName.getAsTemplateDecl());

				if (const CxxDeclName* declName = declNameErased.target<CxxDeclName>())
				{
					std::vector<std::string> templateArguments;
					CxxTemplateArgumentNameResolver resolver(this);
					resolver.ignoreContextDecl(templateSpecializationType->getTemplateName()
												   .getAsTemplateDecl()
												   ->getTemplatedDecl());
					for (const clang::TemplateArgument &templateArgument : templateSpecializationType->template_arguments())
					{
						if (templateArgument.isDependent())
						{
							return std::make_unique<CxxTypeName>(
								declName->getName(),
								declName->getTemplateParameterNames(),
								declName->getParent());
						}
						templateArguments.push_back(
							resolver.getTemplateArgumentName(templateArgument));
					}

					return std::make_unique<CxxTypeName>(
						declName->getName(), std::move(templateArguments), declName->getParent());
				}
				else
				{
					if (const clang::DependentTemplateName* dependentTemplateName =
							templateName.getAsDependentTemplateName())
					{
						std::vector<std::string> templateArguments;
						CxxTemplateArgumentNameResolver resolver(this);
						for (const clang::TemplateArgument& templateArgument:
							 templateSpecializationType->template_arguments())
						{
							templateArguments.push_back(
								resolver.getTemplateArgumentName(templateArgument));
						}

						CxxName specifierName =
							CxxSpecifierNameResolver(this).getName(dependentTemplateName->getQualifier());

						if (const clang::IdentifierInfo* identifier =
								dependentTemplateName->getName().getIdentifier())
						{
							return std::make_unique<CxxTypeName>(
								identifier->getName().str(),
								std::move(templateArguments),
								std::move(specifierName));
						}
					}

					LOG_WARNING("no decl found");
				}
			}
			break;
		}
		case clang::Type::TemplateTypeParm:
		{
			CxxName declNameErased = resolveDeclName(
				clang::dyn_cast<clang::TemplateTypeParmType>(type)->getDecl());
			if (const CxxDeclName* declName = declNameErased.target<CxxDeclName>())
			{
				return std::make_unique<CxxTypeName>(
					declName->getName(), declName->getTemplateParameterNames(), declName->getParent());
			}
			break;
		}
		case clang::Type::SubstTemplateTypeParm:
		{
			return getName(type->getAs<clang::SubstTemplateTypeParmType>()->getReplacementType());
		}
		case clang::Type::DependentName:
		{
			const clang::DependentNameType* dependentType =
				clang::dyn_cast<clang::DependentNameType>(type);
			CxxName specifierName = CxxSpecifierNameResolver(this).getName(
				dependentType->getQualifier());
			return std::make_unique<CxxTypeName>(
				dependentType->getIdentifier()->getName().str(),
				std::vector<std::string>(),
				std::move(specifierName));
		}
#if LLVM_VERSION_MAJOR < 22
		case clang::Type::DependentTemplateSpecialization:
		{
			const clang::DependentTemplateSpecializationType* dependentType =
				clang::dyn_cast<clang::DependentTemplateSpecializationType>(type);
			const auto& depName = dependentType->getDependentTemplateName();
			CxxName specifierName =
				CxxSpecifierNameResolver(this).getName(depName.getQualifier());

			std::vector<std::string> templateArguments;
			CxxTemplateArgumentNameResolver resolver(this);
			for (const clang::TemplateArgument& templateArgument : dependentType->template_arguments())
				templateArguments.push_back(resolver.getTemplateArgumentName(templateArgument));

			if (const clang::IdentifierInfo* id = depName.getName().getIdentifier())
			{
				return std::make_unique<CxxTypeName>(
					id->getName().str(), std::move(templateArguments), std::move(specifierName));
			}
			break;
		}
#endif
		case clang::Type::PackExpansion:
		{
			return getName(clang::dyn_cast<clang::PackExpansionType>(type)->getPattern());
		}
		case clang::Type::Auto:
		{
			const AutoType *autoType = cast<AutoType>(type);
			if (QualType deducedType = autoType->getDeducedType(); !deducedType.isNull())
			{
				return getName(deducedType);
			}
			else
			{
				// Actual type is resolved in CxxAstVisitorComponentIndexer::visitVarDecl
				switch (autoType->getKeyword())
				{
					case AutoTypeKeyword::Auto:
						return make_unique<CxxTypeName>("auto");
					case AutoTypeKeyword::DecltypeAuto:
						return make_unique<CxxTypeName>("decltype(auto)");
					case AutoTypeKeyword::GNUAutoType:
						return make_unique<CxxTypeName>("__auto_type"); // GNU C extension
					default:
						LOG_WARNING("Unknown auto type keyword encountered");
						return make_unique<CxxTypeName>("auto");
				}
			}
		}
		case clang::Type::Decltype:
		{
			const clang::QualType underlyingType =
				clang::dyn_cast<clang::DecltypeType>(type)->getUnderlyingType();
			if (!underlyingType.isNull())
			{
				return getName(underlyingType);
			}
			// A dependent decltype (e.g. the 'decltype(void(sizeof(T)))' argument of a
			// partial specialization) is a DependentDecltypeType, which carries no
			// underlying type until instantiation. Print the decltype expression instead.
			clang::PrintingPolicy pp = makePrintingPolicyForCPlusPlus();
			clang::SmallString<64> Buf;
			llvm::raw_svector_ostream StrOS(Buf);
			clang::QualType::print(type, clang::Qualifiers(), StrOS, pp, clang::Twine());
			return std::make_unique<CxxTypeName>(StrOS.str().str());
		}
		case clang::Type::FunctionProto:
		{
			const clang::FunctionProtoType* protoType = clang::dyn_cast<clang::FunctionProtoType>(
				type);
			std::string nameString =
				CxxTypeName::makeUnsolvedIfNull(getName(protoType->getReturnType()))->toString();
			nameString += "(";
			for (unsigned i = 0; i < protoType->getNumParams(); i++)
			{
				if (i != 0)
				{
					nameString += ", ";
				}
				nameString +=
					CxxTypeName::makeUnsolvedIfNull(getName(protoType->getParamType(i)))->toString();
			}
			nameString += ")";

			return std::make_unique<CxxTypeName>(std::move(nameString));
		}
		case clang::Type::Adjusted:
		case clang::Type::Decayed:
		{
			return getName(type->getAs<clang::AdjustedType>()->getOriginalType());
		}
		default:
		{
			const std::string typeClassName = type->getTypeClassName();
			LOG_INFO("Unhandled kind of type encountered: " + typeClassName);
			clang::PrintingPolicy pp = makePrintingPolicyForCPlusPlus();

			clang::SmallString<64> Buf;
			llvm::raw_svector_ostream StrOS(Buf);
			clang::QualType::print(type, clang::Qualifiers(), StrOS, pp, clang::Twine());
			std::string nameString = StrOS.str().str();

			return std::make_unique<CxxTypeName>(std::move(nameString));
		}
		}
	}
	return nullptr;
}
