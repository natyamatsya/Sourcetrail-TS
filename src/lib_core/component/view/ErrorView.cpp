#include "ErrorView.h"

ErrorView::ErrorView(ViewLayout* viewLayout): View(viewLayout) {}

ErrorView::~ErrorView() = default;

std::string ErrorView::getName() const
{
	return "Errors";
}
