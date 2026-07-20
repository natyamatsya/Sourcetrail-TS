#ifndef SINGLE_FRONTEND_ACTION_FACTORY
#define SINGLE_FRONTEND_ACTION_FACTORY

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/Tooling/Tooling.h>
#endif

SRCTRL_EXPORT class SingleFrontendActionFactory: public clang::tooling::FrontendActionFactory
{
public:
	SingleFrontendActionFactory(clang::FrontendAction* action);
	std::unique_ptr<clang::FrontendAction> create() override;

private:
	clang::FrontendAction* m_action;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "SingleFrontendActionFactory.inl"
#endif

#endif	  // SINGLE_FRONTEND_ACTION_FACTORY
