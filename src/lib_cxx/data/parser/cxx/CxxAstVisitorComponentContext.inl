// Inline implementations for CxxAstVisitorComponentContext.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "data/parser/cxx/CxxAstVisitor.h"
#endif

inline CxxAstVisitorComponentContext::CxxAstVisitorComponentContext(CxxAstVisitor* astVisitor)
	: CxxAstVisitorComponent(astVisitor)
{
}

inline const clang::NamedDecl* CxxAstVisitorComponentContext::getTopmostContextDecl(const size_t skip) const
{
	size_t skipped = 0;

	for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); it++)
	{
		if (*it)
		{
			if (skipped < skip)
			{
				skipped++;
				continue;
			}
			if (const clang::NamedDecl* decl = clang::dyn_cast<const clang::NamedDecl*>(*it))
			{
				return decl;
			}
		}
	}
	return nullptr;
}

inline CxxContext CxxAstVisitorComponentContext::getContext(const size_t skip) const
{
	size_t skipped = 0;

	for (auto it = m_contextStack.rbegin(); it != m_contextStack.rend(); it++)
	{
		if (*it)
		{
			if (skipped < skip)
			{
				skipped++;
				continue;
			}
			return *it;
		}
	}
	return {};
}

inline void CxxAstVisitorComponentContext::beginTraverseDecl(clang::Decl* d)
{
	CxxContext context;

	if (clang::isa_and_nonnull<clang::NamedDecl>(d) &&
		!clang::isa<clang::ParmVarDecl>(d) &&	 // no parameter
		(!clang::isa<clang::VarDecl>(d) || d->getParentFunctionOrMethod() == nullptr) &&  // no local variable
		!clang::isa<clang::UsingDirectiveDecl>(d) &&		 // no using directive decl
		!clang::isa<clang::UsingDecl>(d) &&					 // no using decl
		!clang::isa<clang::NamespaceDecl>(d) &&				 // no namespace
		!clang::isa<clang::NonTypeTemplateParmDecl>(d) &&	 // no template params
		!clang::isa<clang::TemplateTypeParmDecl>(d) &&		 // no template params
		!clang::isa<clang::TemplateTemplateParmDecl>(d)		 // no template params
	)
	{
		clang::NamedDecl* nd = clang::dyn_cast<clang::NamedDecl>(d);
		context = CxxContext(nd);
	}

	m_contextStack.push_back(context);
}

inline void CxxAstVisitorComponentContext::endTraverseDecl(clang::Decl*  /*d*/)
{
	m_contextStack.pop_back();
}

inline void CxxAstVisitorComponentContext::beginTraverseTypeLoc(const clang::TypeLoc& tl)
{
	CxxContext context;
	// clang::TypeLoc::TypeLocClass tlcc = tl.getTypeLocClass();
	if (getAstVisitor()->shouldHandleTypeLoc(tl))
	{
		bool recordContext = true;
		if (tl.getTypeLocClass() == clang::TypeLoc::TemplateSpecialization)
		{
			const clang::TemplateSpecializationTypeLoc& tstl =
				tl.castAs<clang::TemplateSpecializationTypeLoc>();
			const clang::TemplateSpecializationType* tst = tstl.getTypePtr();
			if (tst && tst->getTemplateName().isDependent()
#if LLVM_VERSION_MAJOR >= 22
				// In 22+ the former DependentTemplateSpecializationType (a member
				// template of a dependent base, "A<U>::template type<float>") is a
				// TemplateSpecializationType with a dependent name too; it names a
				// real symbol and keeps its context (as upstream did for the
				// separate class). Only a template template PARAMETER ("T<int>")
				// is a local symbol without a context of its own.
				&& clang::isa_and_nonnull<clang::TemplateTemplateParmDecl>(
					   tst->getTemplateName().getAsTemplateDecl())
#endif
			)
			{
				recordContext = false;
			}
		}
		if (recordContext)
		{
			context = CxxContext(tl.getTypePtr());
		}
	}

	m_contextStack.push_back(context);
}

inline void CxxAstVisitorComponentContext::endTraverseTypeLoc(const clang::TypeLoc&  /*tl*/)
{
	m_contextStack.pop_back();
}

inline void CxxAstVisitorComponentContext::beginTraverseLambdaExpr(clang::LambdaExpr* s)
{
	m_contextStack.push_back(CxxContext(s->getCallOperator()));
}

inline void CxxAstVisitorComponentContext::endTraverseLambdaExpr(clang::LambdaExpr*  /*s*/)
{
	m_contextStack.pop_back();
}

inline void CxxAstVisitorComponentContext::beginTraverseFunctionDecl(clang::FunctionDecl* d)
{
	m_templateArgumentContext.push_back(CxxContext(d));
}

inline void CxxAstVisitorComponentContext::endTraverseFunctionDecl(clang::FunctionDecl*  /*d*/)
{
	m_templateArgumentContext.pop_back();
}

inline void CxxAstVisitorComponentContext::beginTraverseClassTemplateSpecializationDecl(
	clang::ClassTemplateSpecializationDecl* d)
{
	m_templateArgumentContext.push_back(CxxContext(d));
}

inline void CxxAstVisitorComponentContext::endTraverseClassTemplateSpecializationDecl(
	clang::ClassTemplateSpecializationDecl*  /*d*/)
{
	m_templateArgumentContext.pop_back();
}

inline void CxxAstVisitorComponentContext::beginTraverseClassTemplatePartialSpecializationDecl(
	clang::ClassTemplatePartialSpecializationDecl* d)
{
	m_templateArgumentContext.push_back(CxxContext(d));
}

inline void CxxAstVisitorComponentContext::endTraverseClassTemplatePartialSpecializationDecl(
	clang::ClassTemplatePartialSpecializationDecl*  /*d*/)
{
	m_templateArgumentContext.pop_back();
}

inline void CxxAstVisitorComponentContext::beginTraverseDeclRefExpr(clang::DeclRefExpr* s)
{
	m_templateArgumentContext.push_back(CxxContext(
		s->getDecl()));	   // e.g. used for recording usage of template arguments within function calls
}

inline void CxxAstVisitorComponentContext::endTraverseDeclRefExpr(clang::DeclRefExpr*  /*s*/)
{
	m_templateArgumentContext.pop_back();
}

inline void CxxAstVisitorComponentContext::beginTraverseTemplateSpecializationTypeLoc(
	const clang::TemplateSpecializationTypeLoc& loc)
{
	bool recordContext = true;
	const clang::TemplateSpecializationType* tst = loc.getTypePtr();
	if (tst && tst->getTemplateName().isDependent())
	{
		recordContext = false;
	}

	if (recordContext)
	{
		m_templateArgumentContext.push_back(CxxContext(loc.getTypePtr()));
	}
	else
	{
		m_templateArgumentContext.push_back(CxxContext());
	}
}

inline void CxxAstVisitorComponentContext::endTraverseTemplateSpecializationTypeLoc(
	const clang::TemplateSpecializationTypeLoc&  /*loc*/)
{
	m_templateArgumentContext.pop_back();
}

inline void CxxAstVisitorComponentContext::beginTraverseUnresolvedLookupExpr(
	clang::UnresolvedLookupExpr*  /*e*/)	   // TODO: do this for unresolved and dependent stuff
{
	m_templateArgumentContext.push_back(CxxContext());
}

inline void CxxAstVisitorComponentContext::endTraverseUnresolvedLookupExpr(clang::UnresolvedLookupExpr*  /*e*/)
{
	m_templateArgumentContext.pop_back();
}

inline void CxxAstVisitorComponentContext::beginTraverseTemplateArgumentLoc(
	const clang::TemplateArgumentLoc&  /*loc*/)
{
	CxxContext context;

	if (!m_templateArgumentContext.empty())
	{
		context = m_templateArgumentContext.back();
	}

	m_contextStack.push_back(context);
}

inline void CxxAstVisitorComponentContext::endTraverseTemplateArgumentLoc(const clang::TemplateArgumentLoc&  /*loc*/)
{
	m_contextStack.pop_back();
}
