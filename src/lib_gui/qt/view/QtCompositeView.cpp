#include "QtCompositeView.h"
#include "UiPost.h"

#include <QBoxLayout>

#ifndef SRCTRL_MODULE_BUILD
#include "ColorScheme.h"
#endif
#include "QtViewWidgetWrapper.h"
#include "utilityQt.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

QtCompositeView::QtCompositeView(
	ViewLayout* viewLayout, CompositeDirection direction, const std::string& name, TabId tabId)
	: CompositeView(viewLayout, direction, name, tabId)
{
	using enum CompositeView::CompositeDirection;
	QBoxLayout* topLayout = new QVBoxLayout();
	topLayout->setSpacing(0);
	topLayout->setContentsMargins(0, 0, 0, 0);

	const size_t indicatorHeight = 3;

	{
		m_focusIndicator = new QWidget();
		m_focusIndicator->setObjectName(QStringLiteral("focus_indicator"));
		m_focusIndicator->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
		m_focusIndicator->setFixedHeight(indicatorHeight);
		topLayout->addWidget(m_focusIndicator);
	}

	if (getDirection() == DIRECTION_HORIZONTAL)
	{
		m_layout = new QHBoxLayout();
	}
	else
	{
		m_layout = new QVBoxLayout();
	}

	m_layout->setSpacing(5);
	m_layout->setContentsMargins(8, 8 - indicatorHeight, 8, 7);
	m_layout->setAlignment(Qt::AlignTop);

	topLayout->addLayout(m_layout);

	m_widget = new QWidget();
	m_widget->setLayout(topLayout);

	refreshView();
}

void QtCompositeView::createWidgetWrapper()
{
	setWidgetWrapper(std::make_shared<QtViewWidgetWrapper>(m_widget));
}

void QtCompositeView::refreshView()
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]() {
		utility::setWidgetBackgroundColor(
			m_widget, ColorScheme::getInstance()->getColor("search/background"));
	});

	showFocusIndicator(false);
}

void QtCompositeView::addViewWidget(View* view)
{
	m_layout->addWidget(QtViewWidgetWrapper::getWidgetOfView(view));
}

void QtCompositeView::showFocusIndicator(bool focus)
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]() {
		const std::string& colorName = focus ? "window/focus" : "search/background";
		utility::setWidgetBackgroundColor(
			m_focusIndicator, ColorScheme::getInstance()->getColor(colorName));
	});
}
