#include "GraphView.h"

const char* GraphView::VIEW_NAME = "Graph";

GraphView::GraphView(ViewLayout* viewLayout): View(viewLayout) {}

GraphView::~GraphView() = default;

std::string GraphView::getName() const
{
	return VIEW_NAME;
}
