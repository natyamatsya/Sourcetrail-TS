#ifndef QT_SCALE_FACTOR_H
#define QT_SCALE_FACTOR_H

#include <optional>

// Qt high-DPI scaling is driven purely by the QT_SCALE_FACTOR / QT_AUTO_SCREEN_SCALE_FACTOR environment
// variables (qputenv/qgetenv -- QtCore only, no widgets). These live in the base `lib` rather than lib_gui's
// widget-heavy utilityQt so the shared app bootstrap (setupApp) can set them without dragging Qt Widgets
// into the headless indexer.
namespace utility
{
extern const char QT_AUTO_SCREEN_SCALE_FACTOR[];

bool isQtAutoScreenScaleFactorEnabled();
void setQtAutoScreenScaleFactorEnabled(bool enable);

extern const char QT_SCALE_FACTOR[];

std::optional<double> getQtScaleFactor();
void setQtScaleFactor(double factor);
}	 // namespace utility

#endif	  // QT_SCALE_FACTOR_H
