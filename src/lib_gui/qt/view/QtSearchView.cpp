#include "QtSearchView.h"
#include "UiPost.h"

#include "QtSearchBar.h"
#include "QtViewWidgetWrapper.h"
#include "utilityQt.h"
#include "QtResources.h"

using namespace utility;

QtSearchView::QtSearchView(ViewLayout* viewLayout): SearchView(viewLayout)
{
	m_widget = new QtSearchBar();
}

void QtSearchView::createWidgetWrapper()
{
	setWidgetWrapper(std::make_shared<QtViewWidgetWrapper>(m_widget));
}

void QtSearchView::refreshView()
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [this]() {
		setStyleSheet();
		m_widget->refreshStyle();
	});
}

std::string QtSearchView::getQuery() const
{
	return m_widget->query().toStdString();
}

void QtSearchView::setMatches(const std::vector<SearchMatch>& matches)
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]() { m_widget->setMatches(matches); });
}

void QtSearchView::setFocus()
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [this]() {
		getViewLayout()->showView(this);
		m_widget->setFocus();
	});
}

void QtSearchView::findFulltext()
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [this]() {
		getViewLayout()->showView(this);
		m_widget->findFulltext();
	});
}

void QtSearchView::setAutocompletionList(const std::vector<SearchMatch>& autocompletionList)
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]() { m_widget->setAutocompletionList(autocompletionList); });
}

void QtSearchView::setStyleSheet()
{
	QString css = QtResources::loadStyleSheet(QtResources::SEARCH_VIEW_CSS);

	m_widget->setStyleSheet(css);

	if (m_widget->getCompleterPopup())
	{
		m_widget->getCompleterPopup()->setStyleSheet(css);
	}
}
