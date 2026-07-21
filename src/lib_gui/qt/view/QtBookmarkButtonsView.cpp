#include "QtBookmarkButtonsView.h"
#include "UiPost.h"
#ifndef SRCTRL_MODULE_BUILD
#include "MessageBookmarkBrowse.h"
#include "MessageBookmarkCreate.h"
#include "MessageBookmarkDelete.h"
#include "MessageBookmarkEdit.h"
#endif
#include "QtActions.h"
#include "QtMessageBox.h"
#include "QtResources.h"
#include "QtSearchBarButton.h"
#include "QtViewWidgetWrapper.h"
#include "utilityQt.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
#endif

using namespace utility;

QtBookmarkButtonsView::QtBookmarkButtonsView(ViewLayout* viewLayout)
	: BookmarkButtonsView(viewLayout)
{
	m_widget = new QFrame();
	m_widget->setObjectName(QStringLiteral("bookmark_bar"));

	QBoxLayout* layout = new QHBoxLayout();
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setAlignment(Qt::AlignTop);
	m_widget->setLayout(layout);

	m_createBookmarkButton = new QtSearchBarButton(QtResources::BOOKMARK_VIEW_BOOKMARK_EDIT_BOOKMARK_ICON);
	m_createBookmarkButton->setObjectName(QStringLiteral("bookmark_button"));
	m_createBookmarkButton->setToolTip(QtActions::bookmarkActiveSymbol().tooltip());
	m_createBookmarkButton->setEnabled(false);
	layout->addWidget(m_createBookmarkButton);

	connect(m_createBookmarkButton, &QPushButton::clicked, this, &QtBookmarkButtonsView::createBookmarkClicked);

	m_showBookmarksButton = new QtSearchBarButton(QtResources::BOOKMARK_VIEW_BOOKMARK_LIST_ICON);
	m_showBookmarksButton->setObjectName(QStringLiteral("show_bookmark_button"));
	m_showBookmarksButton->setToolTip(QtActions::bookmarkManager().tooltip());
	layout->addWidget(m_showBookmarksButton);

	connect(m_showBookmarksButton, &QPushButton::clicked, this, &QtBookmarkButtonsView::showBookmarksClicked);
}

void QtBookmarkButtonsView::createWidgetWrapper()
{
	setWidgetWrapper(std::make_shared<QtViewWidgetWrapper>(m_widget));
}

void QtBookmarkButtonsView::refreshView()
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]()
	{
		m_widget->setStyleSheet(QtResources::loadStyleSheet(QtResources::BOOKMARK_VIEW_CSS));
	});
}

void QtBookmarkButtonsView::setCreateButtonState(const MessageBookmarkButtonState::ButtonState& state)
{
	using enum MessageBookmarkButtonState::ButtonState;
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]() {
		m_createButtonState = state;

		m_createBookmarkButton->setIconPath(QtResources::BOOKMARK_VIEW_BOOKMARK_EDIT_BOOKMARK_ICON);

		if (state == CAN_CREATE)
		{
			m_createBookmarkButton->setEnabled(true);
		}
		else if (state == CANNOT_CREATE)
		{
			m_createBookmarkButton->setEnabled(false);
		}
		else if (state == ALREADY_CREATED)
		{
			m_createBookmarkButton->setEnabled(true);

			m_createBookmarkButton->setIconPath(QtResources::BOOKMARK_VIEW_BOOKMARK_ACTIVE);
		}
		else
		{
			m_createBookmarkButton->setEnabled(false);
		}
	});
}

void QtBookmarkButtonsView::createBookmarkClicked()
{
	using enum MessageBookmarkButtonState::ButtonState;
	if (m_createButtonState == CAN_CREATE)
	{
		MessageBookmarkCreate().dispatch();
	}
	else if (m_createButtonState == ALREADY_CREATED)
	{
		QtMessageBox msgBox;
		msgBox.setText(tr("Edit Bookmark"));
		msgBox.setInformativeText(tr("Do you want to edit or delete the bookmark for this symbol?"));
		QPushButton *editButton = msgBox.addButton(tr("Edit"), QtMessageBox::ButtonRole::YesRole);
		QPushButton *deleteButton = msgBox.addButton(tr("Delete"), QtMessageBox::ButtonRole::NoRole);
		QPushButton* cancelButton = msgBox.addButton(tr("Cancel"), QtMessageBox::ButtonRole::RejectRole);
		msgBox.setDefaultButton(cancelButton);
		msgBox.setIcon(QtMessageBox::Icon::Question);

		QAbstractButton *clickedButton = msgBox.execModal();
		if (clickedButton == editButton)
		{
			MessageBookmarkEdit().dispatch();
		}
		else if (clickedButton == deleteButton)
		{
			MessageBookmarkDelete().dispatch();
		}
	}
}

void QtBookmarkButtonsView::showBookmarksClicked()
{
	MessageBookmarkBrowse().dispatch();
}
