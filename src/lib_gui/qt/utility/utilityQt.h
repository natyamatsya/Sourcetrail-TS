#ifndef UTILITY_QT_H
#define UTILITY_QT_H

#include <string>

#include <QVariant>

class FilePath;
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
