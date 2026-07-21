#ifndef GRAPH_VIEW_STYLE_IMPL_H
#define GRAPH_VIEW_STYLE_IMPL_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "Node.h"
#endif

SRCTRL_EXPORT class GraphViewStyleImpl
{
public:
	virtual ~GraphViewStyleImpl() = default;
	virtual float getCharWidth(const std::string& fontName, size_t fontSize) = 0;
	virtual float getCharHeight(const std::string& fontName, size_t fontSize) = 0;
	virtual float getGraphViewZoomDifferenceForPlatform() = 0;
};

#endif	  // GRAPH_VIEW_STYLE_IMPL_H
