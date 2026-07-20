#ifndef RESOURCE_PATHS_H
#define RESOURCE_PATHS_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
#endif

SRCTRL_EXPORT class ResourcePaths
{
public:
	static FilePath getColorSchemesDirectoryPath();
	static FilePath getSyntaxHighlightingRulesDirectoryPath();
	static FilePath getFallbackDirectoryPath();
	static FilePath getFontsDirectoryPath();
	static FilePath getCxxCompilerHeaderDirectoryPath();
};

#include "ResourcePaths.inl"

#endif	  // RESOURCE_PATHS_H
