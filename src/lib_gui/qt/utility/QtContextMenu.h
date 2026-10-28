#ifndef QT_CONTEXT_MENU_H
#define QT_CONTEXT_MENU_H

#include <QAction>
#include <QMenu>
#include <QObject>
#include <memory>

#include "FilePath.h"

class QtContextMenu: public QObject
{
	Q_OBJECT

public:
	QtContextMenu(QContextMenuEvent* event, QWidget* origin);

	void addAction(QAction* action);
	void addUndoActions();
	void addFileActions(const FilePath& filePath);
	// "Open in Editor" for `filePath` at `line` and `column` (1-based); disabled without a file.
	void addOpenInEditorAction(const FilePath& filePath, std::size_t line = 1, std::size_t column = 1);

	static QtContextMenu* getInstance();

	void addSeparator();

	void show();

	static void enableUndo(bool enabled);
	static void enableRedo(bool enabled);

private slots:
	static void undoActionTriggered();
	static void redoActionTriggered();

	static void copyFullPathActionTriggered();
	static void openContainingFolderActionTriggered();
	static void openInEditorActionTriggered();

private:
	QtContextMenu();

	static QtContextMenu* s_instance;

	static QAction* s_undoAction;
	static QAction* s_redoAction;

	static QAction* s_copyFullPathAction;
	static QAction* s_openContainingFolderAction;
	static QAction* s_openInEditorAction;
	static std::size_t s_line;
	static std::size_t s_column;

	static FilePath s_filePath;

	QMenu m_menu;
	QPoint m_point;
};

#endif	  // QT_CONTEXT_MENU_H
