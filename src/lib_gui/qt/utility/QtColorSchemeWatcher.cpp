#include "QtColorSchemeWatcher.h"

#include <memory>
#include <string>

#include <QGuiApplication>
#include <QStyleHints>

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#include "MessageStatus.h"
#include "MessageSwitchColorScheme.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
import srctrl.settings;
#endif

Qt::ColorScheme QtColorSchemeWatcher::systemColorScheme()
{
	return QGuiApplication::styleHints()->colorScheme();
}

FilePath QtColorSchemeWatcher::resolveColorSchemePath()
{
	std::shared_ptr<ApplicationSettings> settings = ApplicationSettings::getInstance();

	if (!settings->getColorSchemeFollowsSystem())
	{
		return settings->getColorSchemePath();
	}

	const std::string schemeName = (systemColorScheme() == Qt::ColorScheme::Dark)
		? settings->getColorSchemeNameDark()
		: settings->getColorSchemeName();

	return settings->getColorSchemePath(schemeName);
}

QtColorSchemeWatcher::QtColorSchemeWatcher(QObject* parent): QObject(parent)
{
	connect(
		QGuiApplication::styleHints(),
		&QStyleHints::colorSchemeChanged,
		this,
		&QtColorSchemeWatcher::onSystemColorSchemeChanged);
}

void QtColorSchemeWatcher::onSystemColorSchemeChanged(Qt::ColorScheme colorScheme)
{
	if (!ApplicationSettings::getInstance()->getColorSchemeFollowsSystem())
	{
		return;
	}

	const bool isDark = (colorScheme == Qt::ColorScheme::Dark);
	MessageStatus(std::string("System appearance changed to ") + (isDark ? "dark" : "light") +
				  " mode, switching color scheme")
		.dispatch();

	MessageSwitchColorScheme(resolveColorSchemePath()).dispatch();
}
