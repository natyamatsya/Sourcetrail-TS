// Inline implementations for SingleFrontendActionFactory.h. Included at the end of that header (classic) or via
// the srctrl.cxx:frontend wrapper (purview); not a standalone TU.

#pragma once

inline SingleFrontendActionFactory::SingleFrontendActionFactory(clang::FrontendAction* action)
	: m_action(action)
{
}

inline std::unique_ptr<clang::FrontendAction> SingleFrontendActionFactory::create()
{
	return std::unique_ptr<clang::FrontendAction>(m_action);
}
