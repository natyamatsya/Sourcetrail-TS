#ifndef UTILITY_QT_H
#define UTILITY_QT_H

#include <memory>
#include <string>

#include <QVariant>

// FilePath is module-owned but GLOBAL-MODULE-attached (SRCTRL_EXPORT = export extern "C++"),
// so this textual fwd decl and the `import srctrl.file;` of a converted includer declare the
// SAME entity -- no guard needed, and moc-generated TUs parse it classically as ever.
class FilePath;
class QAbstractAnimation;
class QColor;
class QIcon;
class QPixmap;
class QString;
class QWidget;
class QtMainWindow;
class ViewLayout;

namespace utility
{
void setWidgetBackgroundColor(QWidget *widget, const std::string &color);
void setWidgetRetainsSpaceWhenHidden(QWidget *widget);

void loadFontsFromDirectory(const FilePath &path, const std::string &extension = ".otf");

QPixmap colorizePixmap(const QPixmap &pixmap, QColor color);
QIcon createButtonIcon(const FilePath &iconPath, const std::string &colorId);

QtMainWindow *getMainWindowforMainView(ViewLayout *viewLayout);

void copyNewFilesFromDirectory(const QString &src, const QString &dst);

// Keeps `hold` until `animation` stops (or goes): a view holds its drawing ticket
// (ActivationStateMachine::drawing) through the animation that shows the result.
void holdWhileRunning(QAbstractAnimation *animation, std::shared_ptr<void> hold);

// The window in front: the active modal one (a dialog, a message box), else the active window;
// nullptr when the application is not active (an agent drives it headless). Boxes are parented
// to it, so they are centred on the window they are about.
QWidget *windowInFront();

// Qt high-DPI scale-factor env helpers moved to lib/utility/qtScaleFactor.h (QtCore-only, so the headless
// indexer's bootstrap doesn't need lib_gui). #include "qtScaleFactor.h" for isQtAutoScreenScaleFactorEnabled/
// setQtAutoScreenScaleFactorEnabled/getQtScaleFactor/setQtScaleFactor.



template <typename T>
T qt_variant_cast(const QVariant &variant)
{
	Q_ASSERT(variant.canConvert<T>());
	return qvariant_cast<T>(variant);
}

}

#endif // UTILITY_QT_H
