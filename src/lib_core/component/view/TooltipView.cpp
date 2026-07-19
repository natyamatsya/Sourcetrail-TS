#include "TooltipView.h"

TooltipView::TooltipView(ViewLayout* viewLayout): View(viewLayout) {}

TooltipView::~TooltipView() = default;

std::string TooltipView::getName() const
{
	return "TooltipView";
}
