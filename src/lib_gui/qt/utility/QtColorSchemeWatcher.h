#ifndef QT_COLOR_SCHEME_WATCHER_H
#define QT_COLOR_SCHEME_WATCHER_H

#include <QObject>
#include <Qt>

#include "FilePath.h"

//! Bridges the operating system's Day/Night (light/dark) appearance to
//! Sourcetrail's color schemes using Qt's QStyleHints::colorScheme() support
//! (available since Qt 6.5).
//!
//! While ApplicationSettings::getColorSchemeFollowsSystem() is enabled, the
//! effective color scheme is resolved from the current system appearance:
//! getColorSchemeName() (the "day" scheme) while the system is light, and
//! getColorSchemeNameDark() while it is dark. When the user toggles the system
//! appearance at runtime, the watcher dispatches a MessageSwitchColorScheme so
//! the whole UI updates live.
class QtColorSchemeWatcher: public QObject
{
	Q_OBJECT

public:
	//! The system appearance reported by Qt, or Qt::ColorScheme::Unknown when the
	//! platform does not expose one (treated as "light").
	static Qt::ColorScheme systemColorScheme();

	//! Resolves the color scheme that should currently be active, honoring the
	//! follow-system setting. Safe to call before a QtColorSchemeWatcher exists;
	//! installed as Application's color scheme path provider.
	static FilePath resolveColorSchemePath();

	explicit QtColorSchemeWatcher(QObject* parent = nullptr);

private:
	void onSystemColorSchemeChanged(Qt::ColorScheme colorScheme);
};

#endif	  // QT_COLOR_SCHEME_WATCHER_H
