#include "QtScreenshot.h"

#include <QApplication>
#include <QMainWindow>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

#include "logging.h"

namespace utility::qt
{
namespace
{
// The app has exactly one top-level QMainWindow (QtMainWindow); find it so we do
// not have to thread a reference through Application/MainView just to grab it.
QMainWindow* findMainWindow()
{
	for (QWidget* widget: QApplication::topLevelWidgets())
	{
		if (auto* mainWindow = qobject_cast<QMainWindow*>(widget))
		{
			return mainWindow;
		}
	}
	return nullptr;
}

void captureAndQuit(const QString& pngPath)
{
	QMainWindow* mainWindow = findMainWindow();
	if (mainWindow == nullptr)
	{
		LOG_ERROR("Screenshot: no main window found to capture");
	}
	else
	{
		const QPixmap pixmap = mainWindow->grab();
		if (pixmap.isNull() || !pixmap.save(pngPath, "PNG"))
		{
			LOG_ERROR("Screenshot: failed to save \"" + pngPath.toStdString() + "\"");
		}
		else
		{
			LOG_INFO(
				"Screenshot saved: \"" + pngPath.toStdString() + "\" (" +
				std::to_string(pixmap.width()) + "x" + std::to_string(pixmap.height()) + ")");
		}
	}
	QApplication::quit();
}
}	 // namespace

void scheduleScreenshotAndQuit(const std::string& pngPath, int delayMs)
{
	const QString path = QString::fromStdString(pngPath);
	QTimer::singleShot(delayMs, [path]() { captureAndQuit(path); });
}
}	 // namespace utility::qt
