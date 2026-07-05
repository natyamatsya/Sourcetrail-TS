#include "QtScrollSpeedChangeListener.h"

#include <cmath>

#include <QScrollBar>

#include "ApplicationSettings.h"
#include "UiPost.h"

QtScrollSpeedChangeListener::QtScrollSpeedChangeListener() = default;

void QtScrollSpeedChangeListener::setScrollBar(QScrollBar* scrollbar)
{
	m_scrollBar = scrollbar;
	m_singleStep = scrollbar->singleStep();

	doChangeScrollSpeed(ApplicationSettings::getInstance()->getScrollSpeed());
}

void QtScrollSpeedChangeListener::handleMessage(MessageScrollSpeedChange* message)
{
	execution::qt::onUi(m_scrollBar, [this, scrollSpeed = message->scrollSpeed]() { doChangeScrollSpeed(scrollSpeed); });
}

void QtScrollSpeedChangeListener::doChangeScrollSpeed(float scrollSpeed)
{
	if (m_scrollBar)
	{
		m_scrollBar->setSingleStep(static_cast<int>(std::ceil(m_singleStep * scrollSpeed)));
	}
}
