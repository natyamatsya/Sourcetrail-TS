#include "QtContextMenu.h"

#ifndef SRCTRL_MODULE_BUILD
#include "MessageHistoryRedo.h"
#include "MessageHistoryUndo.h"
#include "MessageOpenInEditor.h"
#endif
#include "logging.h"
#include "QtActions.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
#endif

QtContextMenu* QtContextMenu::s_instance;

QAction* QtContextMenu::s_undoAction;
QAction* QtContextMenu::s_redoAction;

QAction* QtContextMenu::s_copyFullPathAction;
QAction* QtContextMenu::s_openContainingFolderAction;
QAction* QtContextMenu::s_openInEditorAction;
std::size_t QtContextMenu::s_line = 1;
std::size_t QtContextMenu::s_column = 1;

FilePath QtContextMenu::s_filePath;

QtContextMenu::QtContextMenu(QContextMenuEvent* event, QWidget* origin)
	: m_menu(new QMenu(origin)), m_point(event->globalPos())
{
	getInstance();
}

QtContextMenu::~QtContextMenu()
{
	delete m_menu.data();	 // null when its origin took it along
}

void QtContextMenu::addAction(QAction* action)
{
	if (m_menu)
	{
		m_menu->addAction(action);
	}
}

void QtContextMenu::enableUndo(bool enabled)
{
	s_undoAction->setEnabled(enabled);
}

void QtContextMenu::enableRedo(bool enabled)
{
	s_redoAction->setEnabled(enabled);
}

void QtContextMenu::addUndoActions()
{
	addAction(s_undoAction);
	addAction(s_redoAction);
}

void QtContextMenu::addFileActions(const FilePath& filePath)
{
	s_filePath = filePath;

	s_copyFullPathAction->setEnabled(!s_filePath.empty());
	s_openContainingFolderAction->setEnabled(!s_filePath.empty());

	addAction(s_copyFullPathAction);
	addAction(s_openContainingFolderAction);
}

void QtContextMenu::addOpenInEditorAction(const FilePath& filePath, std::size_t line, std::size_t column)
{
	s_filePath = filePath;
	s_line = line;
	s_column = column;

	s_openInEditorAction->setEnabled(!s_filePath.empty());
	addAction(s_openInEditorAction);
}

QtContextMenu* QtContextMenu::getInstance()
{
	if (s_instance == nullptr)
	{
		s_instance = new QtContextMenu();

		s_undoAction = new QAction(QtActions::back().menuText(), s_instance);
		s_undoAction->setStatusTip(tr("Go back to last active symbol"));
		s_undoAction->setToolTip(tr("Go back to last active symbol"));
		connect(s_undoAction, &QAction::triggered, s_instance, &QtContextMenu::undoActionTriggered);
		s_undoAction->setEnabled(false);	// no history yet, as the Back button says

		s_redoAction = new QAction(QtActions::forward().menuText(), s_instance);
		s_redoAction->setStatusTip(tr("Go forward to next active symbol"));
		s_redoAction->setToolTip(tr("Go forward to next active symbol"));
		connect(s_redoAction, &QAction::triggered, s_instance, &QtContextMenu::redoActionTriggered);
		s_redoAction->setEnabled(false);

		s_copyFullPathAction = new QAction(tr("Copy Full Path"), s_instance);
		s_copyFullPathAction->setStatusTip(tr("Copies the path of this file to the clipboard"));
		s_copyFullPathAction->setToolTip(tr("Copies the path of this file to the clipboard"));
		connect(s_copyFullPathAction, &QAction::triggered, s_instance, &QtContextMenu::copyFullPathActionTriggered);

		s_openContainingFolderAction = new QAction(tr("Open Containing Folder..."), s_instance);
		s_openContainingFolderAction->setStatusTip(tr("Opens the folder that contains this file"));
		s_openContainingFolderAction->setToolTip(tr("Opens the folder that contains this file"));
		connect(s_openContainingFolderAction, &QAction::triggered, s_instance, &QtContextMenu::openContainingFolderActionTriggered);

		s_openInEditorAction = new QAction(QtActions::openInEditor().menuText(), s_instance);
		s_openInEditorAction->setStatusTip(tr("Opens this file at this line in the editor (Preferences: Editor command)"));
		s_openInEditorAction->setToolTip(tr("Opens this file at this line in the editor (Preferences: Editor command)"));
		connect(s_openInEditorAction, &QAction::triggered, s_instance, &QtContextMenu::openInEditorActionTriggered);
	}

	return s_instance;
}

void QtContextMenu::addSeparator()
{
	if (m_menu)
	{
		m_menu->addSeparator();
	}
}

void QtContextMenu::show()
{
	if (m_menu)
	{
		m_menu->exec(m_point);
	}
}

void QtContextMenu::undoActionTriggered()
{
	MessageHistoryUndo().dispatch();
}

void QtContextMenu::redoActionTriggered()
{
	MessageHistoryRedo().dispatch();
}

void QtContextMenu::copyFullPathActionTriggered()
{
	QApplication::clipboard()->setText(
		QDir::toNativeSeparators(QString::fromStdString(s_filePath.str())));
}

void QtContextMenu::openContainingFolderActionTriggered()
{
	FilePath dir = s_filePath.getParentDirectory();
	if (dir.exists())
	{
		QDesktopServices::openUrl(
			QUrl(QString::fromStdString("file:///" + dir.str()), QUrl::TolerantMode));
	}
	else
	{
		LOG_ERROR("Unable to open directory: " + dir.str());
	}
}

void QtContextMenu::openInEditorActionTriggered()
{
	MessageOpenInEditor(s_filePath, static_cast<unsigned int>(s_line), static_cast<unsigned int>(s_column)).dispatch();
}

QtContextMenu::QtContextMenu() = default;
