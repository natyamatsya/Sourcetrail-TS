#include "QtUndoRedoView.h"
#include "UiPost.h"

#include "utilityQt.h"
#include "QtViewWidgetWrapper.h"
#include "QtResources.h"
#include "TabIds.h"
#include "UndoRedoController.h"

using namespace utility;

QtUndoRedoView::QtUndoRedoView(ViewLayout *viewLayout)
	: UndoRedoView(viewLayout)
{
	m_widget = new QtUndoRedo();
}

void QtUndoRedoView::createWidgetWrapper()
{
	setWidgetWrapper(std::make_shared<QtViewWidgetWrapper>(m_widget));
}

void QtUndoRedoView::refreshView()
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]()
	{
		m_widget->setStyleSheet(QtResources::loadStyleSheet(QtResources::UNDOREDO_VIEW_CSS));
	});
}

bool QtUndoRedoView::isShownTab()
{
	const UndoRedoController* controller = getController();
	return controller == nullptr || controller->getTabId() == TabIds::currentTab();
}

void QtUndoRedoView::setRedoButtonEnabled(bool enabled)
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]()
	{
		m_widget->setRedoButtonEnabled(enabled, isShownTab());
	});
}

void QtUndoRedoView::setUndoButtonEnabled(bool enabled)
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]()
	{
		m_widget->setUndoButtonEnabled(enabled, isShownTab());
	});
}

void QtUndoRedoView::updateHistory(const std::vector<SearchMatch> &searchMatches, size_t currentIndex)
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]()
	{
		m_widget->updateHistory(searchMatches, currentIndex);
	});
}
