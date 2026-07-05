#include "QtRefreshView.h"
#include "UiPost.h"

#include "MessageIndexingShowDialog.h"
#include "MessageRefresh.h"
#include "QtActions.h"
#include "QtResources.h"
#include "QtSearchBarButton.h"
#include "QtViewWidgetWrapper.h"
#include "utilityQt.h"

#include <QFrame>
#include <QHBoxLayout>

using namespace utility;

QtRefreshView::QtRefreshView(ViewLayout* viewLayout): RefreshView(viewLayout)
{
	m_widget = new QFrame();
	m_widget->setObjectName(QStringLiteral("refresh_bar"));

	QBoxLayout* layout = new QHBoxLayout();
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setAlignment(Qt::AlignTop);

	QtSearchBarButton* refreshButton = new QtSearchBarButton(QtResources::REFRESH_VIEW_REFRESH);
	refreshButton->setObjectName(QStringLiteral("refresh_button"));
	refreshButton->setToolTip(QtActions::refresh().tooltip());
	QWidget::connect(refreshButton, &QPushButton::clicked, []()
	{
		MessageIndexingShowDialog().dispatch();
		MessageRefresh().dispatch();
	});

	layout->addWidget(refreshButton);
	m_widget->setLayout(layout);
}

void QtRefreshView::createWidgetWrapper()
{
	setWidgetWrapper(std::make_shared<QtViewWidgetWrapper>(m_widget));
}

void QtRefreshView::refreshView()
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [this]()
	{
		m_widget->setStyleSheet(QtResources::loadStyleSheet(QtResources::REFRESH_VIEW_CSS));
	});
}
