#include "QtRefreshView.h"
#include "UiPost.h"

#ifndef SRCTRL_MODULE_BUILD
#include "MessageIndexingShowDialog.h"
#include "MessageRefresh.h"
#endif
#include "QtActions.h"
#include "QtResources.h"
#include "QtSearchBarButton.h"
#include "QtViewWidgetWrapper.h"
#include "utilityQt.h"

#include <QFrame>
#include <QLabel>
#include <QGuiApplication>
#include <QHBoxLayout>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
#endif

using namespace utility;

QtRefreshView::QtRefreshView(ViewLayout* viewLayout): RefreshView(viewLayout)
{
	m_widget = new QFrame();
	m_widget->setObjectName(QStringLiteral("refresh_bar"));

	QBoxLayout* layout = new QHBoxLayout();
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setAlignment(Qt::AlignVCenter);

	QtSearchBarButton* refreshButton = new QtSearchBarButton(QtResources::REFRESH_VIEW_REFRESH);
	m_refreshButton = refreshButton;
	refreshButton->setObjectName(QStringLiteral("refresh_button"));
	refreshButton->setToolTip(QtActions::refresh().tooltip());
	refreshButton->setAccessibleName(QtActions::refresh().name());
	// As the menu's Refresh; with Alt (Option) held, the mode choice opens, as Full
	// Refresh's does.
	QWidget::connect(refreshButton, &QPushButton::clicked, []()
	{
		MessageIndexingShowDialog().dispatch();
		if (QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier))
		{
			MessageRefresh().dispatch();
		}
		else
		{
			MessageRefresh().direct().dispatch();
		}
	});

	layout->addWidget(refreshButton);
	m_widget->setLayout(layout);

	m_outdatedDot = new QLabel(refreshButton);
	m_outdatedDot->setObjectName(QStringLiteral("outdated_dot"));
	m_outdatedDot->setAccessibleName(QStringLiteral("Indexed files changed"));
	m_outdatedDot->setFixedSize(7, 7);
	m_outdatedDot->move(19, 2);	// the button's top right corner
	m_outdatedDot->setAttribute(Qt::WA_TransparentForMouseEvents);
	m_outdatedDot->hide();
}

void QtRefreshView::handleMessage(MessageIndexOutdated* message)
{
	const std::size_t count = message->changedFileCount;
	execution::qt::onUi(m_widget, [this, count]() {
		m_outdatedDot->setVisible(count > 0);
		m_refreshButton->setToolTip(
			count == 0 ? QtActions::refresh().tooltip()
					   : QtActions::refresh().tooltip() + QStringLiteral(" \u2013 ") +
				(count == 1 ? QStringLiteral("1 file changed") : QStringLiteral("%1 files changed").arg(count)));
		m_refreshButton->setAccessibleDescription(
			count == 0 ? QString() : QStringLiteral("%1 indexed files changed").arg(count));
	});
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
