#include "QtHelpButton.h"

#include "QtResources.h"

QtHelpButton::QtHelpButton(const QtHelpButtonInfo& info, QWidget* parent)
	: QtIconButton(QtResources::WINDOW_HELP, QtResources::WINDOW_HELP_HOVER, parent)
	, m_info(info)
{
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	setAttribute(Qt::WA_LayoutUsesWidgetRect);	  // fixes layouting on Mac
	setMouseTracking(true);

	setToolTip(QStringLiteral("help"));
	setIconSize(QSize(16, 16));
	setObjectName(QStringLiteral("helpButton"));

	leaveEvent(nullptr);

	connect(this, &QtHelpButton::clicked, this, &QtHelpButton::handleHelpPress);
}

void QtHelpButton::setMessageBoxParent(QWidget* messageBoxParent)
{
	m_messageBoxParent = messageBoxParent;
}

void QtHelpButton::handleHelpPress()
{
	m_info.displayMessage(m_messageBoxParent ? m_messageBoxParent : window());
}
