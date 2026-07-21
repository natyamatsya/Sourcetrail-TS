#include "QtScrollSpeedChangeListener.h"

#include <cmath>

#include <QScrollBar>

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#endif
#include "UiPost.h"

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

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
