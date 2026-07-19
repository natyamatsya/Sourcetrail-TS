#ifndef TOOLTIP_INFO_H
#define TOOLTIP_INFO_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>

#include "Vector2.h"
#include "types.h"

// SourceLocationFile is modularized (srctrl.data:location); the wrapper `import`s it in the module
// build, so this forward decl is skipped in the purview. Only std::shared_ptr<SourceLocationFile> is
// used here, so an incomplete type is sufficient.
class SourceLocationFile;
#endif

SRCTRL_EXPORT struct TooltipSnippet
{
	std::string code;
	std::shared_ptr<SourceLocationFile> locationFile;
};

SRCTRL_EXPORT struct TooltipInfo
{
	bool isValid() const
	{
		return title.size() || snippets.size();
	}

	std::string title;

	int count = -1;
	std::string countText;

	std::vector<TooltipSnippet> snippets;

	Vec2i offset;
};

#endif	  // TOOLTIP_INFO_H
