#include "RefreshController.h"

#include "RefreshView.h"

RefreshController::RefreshController() = default;

RefreshController::~RefreshController() = default;

void RefreshController::clear() {}

RefreshView* RefreshController::getView()
{
	return Controller::getView<RefreshView>();
}
