#include "StatusView.h"

StatusView::StatusView(ViewLayout* viewLayout): View(viewLayout) {}

StatusView::~StatusView() = default;

std::string StatusView::getName() const
{
	return "Status";
}
