#include "SearchView.h"

#include "SearchController.h"

SearchView::SearchView(ViewLayout* viewLayout): View(viewLayout) {}

SearchView::~SearchView() = default;

std::string SearchView::getName() const
{
	return "Search";
}

SearchController* SearchView::getController()
{
	return View::getController<SearchController>();
}
