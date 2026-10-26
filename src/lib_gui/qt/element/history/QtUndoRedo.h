#ifndef QT_UNDO_REDO_H
#define QT_UNDO_REDO_H

#include <QFrame>

#include "SearchMatch.h"
#include "TimeStamp.h"

class QtSearchBarButton;
class QtHistoryList;

class QtUndoRedo: public QFrame
{
	Q_OBJECT

public:
	QtUndoRedo();
	~QtUndoRedo() override;

	// `shown`: this tab is the one shown, whose history the context menus' Back and
	// Forward follow.
	void setRedoButtonEnabled(bool enabled, bool shown);
	void setUndoButtonEnabled(bool enabled, bool shown);

	void updateHistory(const std::vector<SearchMatch>& searchMatches, size_t currentIndex);

protected:
	void showEvent(QShowEvent* event) override;

private slots:
	void buttonPressed();

	void undoReleased();
	void redoReleased();

	void showHistory();
	void hidHistory();

private:
	QtSearchBarButton* m_undoButton;
	QtSearchBarButton* m_historyButton;
	QtSearchBarButton* m_redoButton;

	std::vector<SearchMatch> m_history;
	size_t m_currentIndex;

	bool m_pressed = false;

	QtHistoryList* m_historyList = nullptr;
	TimeStamp m_historyHiddenAt;
};

#endif	  // QT_UNDO_REDO_H
