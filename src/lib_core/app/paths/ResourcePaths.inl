// Inline implementations for ResourcePaths.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "AppPath.h"
#endif

inline FilePath ResourcePaths::getColorSchemesDirectoryPath()
{
	return AppPath::getSharedDataDirectoryPath().concatenate("data/color_schemes/");
}

inline FilePath ResourcePaths::getSyntaxHighlightingRulesDirectoryPath()
{
	return AppPath::getSharedDataDirectoryPath().concatenate("data/syntax_highlighting_rules/");
}

inline FilePath ResourcePaths::getFallbackDirectoryPath()
{
	return AppPath::getSharedDataDirectoryPath().concatenate("data/fallback/");
}

inline FilePath ResourcePaths::getFontsDirectoryPath()
{
	return AppPath::getSharedDataDirectoryPath().concatenate("data/fonts/");
}

inline FilePath ResourcePaths::getCxxCompilerHeaderDirectoryPath()
{
	return AppPath::getSharedDataDirectoryPath().concatenate("data/cxx/include/").getCanonical();
}
