#include "ScreenSearchView.h"

ScreenSearchView::ScreenSearchView(ViewLayout* viewLayout): View(viewLayout) {}

ScreenSearchView::~ScreenSearchView() = default;

std::string ScreenSearchView::getName() const
{
	return "ScreenSearchView";
}
