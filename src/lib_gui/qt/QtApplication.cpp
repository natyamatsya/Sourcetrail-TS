#include "QtApplication.h"

#include <QFileOpenEvent>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/qtcommon/View.h>
#include <kddockwidgets/qtwidgets/ViewFactory.h>
#include <kddockwidgets/qtwidgets/views/SideBar.h>

#include "MessageCause.h"

#ifndef SRCTRL_MODULE_BUILD
#include "FilePath.h"
#include "LogManager.h"
#include "MessageLoadProject.h"
#include "MessageWindowFocus.h"
#include "ProjectSettings.h"
#include "utilityApp.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.logging;
import srctrl.messaging;
import srctrl.process;
import srctrl.settings;
#endif

namespace
{
// Side bar buttons (an auto-hidden dock's way back) carry only the dock's title.
class QtDockSideBar: public KDDockWidgets::QtWidgets::SideBar
{
public:
	using SideBar::SideBar;

protected:
	KDDockWidgets::SideBarButton* createButton(KDDockWidgets::Core::DockWidget* dockWidget, SideBar* parent) const override
	{
		KDDockWidgets::SideBarButton* button = SideBar::createButton(dockWidget, parent);
		button->setObjectName(dockWidget->uniqueName() + QStringLiteral("_side_bar_button"));
		return button;
	}
};

// KDDockWidgets names its title bar and tab bar buttons by type ("closeButton"), alike
// in every dock, so an agent cannot address one by objectName. Name them after the dock
// instead: "Graph_close_button". In a tab group they act on the whole group and take
// the current tab's name.
class QtDockViewFactory: public KDDockWidgets::QtWidgets::ViewFactory
{
public:
	QString titleBarButtonObjectName(KDDockWidgets::TitleBarButtonType type, KDDockWidgets::Core::DockWidget* dockWidget) const override
	{
		if (dockWidget == nullptr)
		{
			return {};	  // a floating window holding several groups: keep KDDW's name
		}
		QString button;
		switch (type)
		{
			case KDDockWidgets::TitleBarButtonType::Close:
				button = QStringLiteral("close");
				break;
			case KDDockWidgets::TitleBarButtonType::Float:
				button = QStringLiteral("float");
				break;
			case KDDockWidgets::TitleBarButtonType::Minimize:
				button = QStringLiteral("minimize");
				break;
			case KDDockWidgets::TitleBarButtonType::Maximize:
				button = QStringLiteral("maximize");
				break;
			case KDDockWidgets::TitleBarButtonType::AutoHide:
				button = QStringLiteral("auto_hide");
				break;
			default:
				return {};
		}
		return dockWidget->uniqueName() + QLatin1Char('_') + button + QStringLiteral("_button");
	}

	KDDockWidgets::Core::View* createSideBar(KDDockWidgets::Core::SideBar* controller, KDDockWidgets::Core::View* parent) const override
	{
		return new QtDockSideBar(controller, KDDockWidgets::QtCommon::View_qt::asQWidget(parent));
	}
};
}	 // namespace

QtApplication::QtApplication(int& argc, char** argv): QApplication(argc, argv)
{
	// Bring up the KDDockWidgets QtWidgets frontend before any dock/main window is
	// created (Application::createInstance() builds them). This is the GUI-only path;
	// the headless indexer uses QtCoreApplication and never reaches here.
	KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
	KDDockWidgets::Config::self().setViewFactory(new QtDockViewFactory());

	// Docking behaviour tuned for the multi-monitor detach/reattach workflow: floating
	// windows are independent top-levels that can themselves host docks, tabs are
	// reorderable, and views can be collapsed to the side bar (auto-hide).
	auto& dockConfig = KDDockWidgets::Config::self();
	dockConfig.setFlags(
		dockConfig.flags() | KDDockWidgets::Config::Flag_AllowReorderTabs |
		KDDockWidgets::Config::Flag_AutoHideSupport);

	connect(
		this,
		&QGuiApplication::applicationStateChanged,
		this,
		&QtApplication::onApplicationStateChanged);
}

int QtApplication::exec()
{
	return QApplication::exec();
}

// responds to FileOpenEvent specific for Mac
bool QtApplication::event(QEvent* event)
{
	if (event->type() == QEvent::FileOpen)
	{
		QFileOpenEvent* fileEvent = dynamic_cast<QFileOpenEvent*>(event);

		FilePath path(fileEvent->file().toStdString());

		if (path.exists() && ProjectSettings::isProjectFilePath(path))
		{
			MessageLoadProject(path).dispatch();
			return true;
		}
	}

	return QApplication::event(event);
}

// A person's input (spontaneous mouse, key, wheel, touch events from the window
// system) is on nobody's behalf (MessageCause.h), also in the event loop of a modal
// box that a request's work opened and runs under its cause: a person's answer there
// is theirs. An agent's input is posted, not spontaneous, and keeps its request.
// Other spontaneous events keep the cause: Qt delivers some synchronously for the
// app's own calls (closing a dialog sends a QCloseEvent through the window system).
bool QtApplication::notify(QObject* receiver, QEvent* event)
{
	if (event->spontaneous() && event->isInputEvent())
	{
		const CauseScope none(0);
		return QApplication::notify(receiver, event);
	}
	return QApplication::notify(receiver, event);
}

void QtApplication::onApplicationStateChanged(Qt::ApplicationState state)
{
	MessageWindowFocus(state == Qt::ApplicationActive).dispatch();
}
