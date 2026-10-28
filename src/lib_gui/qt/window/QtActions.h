#ifndef QT_KEY_SEQUENCES_H
#define QT_KEY_SEQUENCES_H

#include <QCoreApplication>
#include <QKeySequence>
#include <QList>

class QKeyEvent;

enum class Action {
	Unknown,

	MoveFocusUp,
	MoveFocusDown,
	MoveFocusLeft,
	MoveFocusRight,
	SwitchGraphCodeFocus,

	MoveViewUp,
	MoveViewDown,
	MoveViewLeft,
	MoveViewRight,

	MoveReferenceFocusUp,
	MoveReferenceFocusDown,
	MoveReferenceFocusLeft,
	MoveReferenceFocusRight,

	ActivateFocus,
	ActivateFocusInNewTab,
	ExpandFocus,

	RedoHistory,
	UndoHistory,

	ZoomIn,
	ZoomOut,
	ResetZoom,

	Cancel,
	CloseTab,
	RefreshUI,
	SearchScreen,
	CopySelection
};

class QtActions final
{
	Q_DECLARE_TR_FUNCTIONS(QtActions)

public:
	static Action detectAction(const QKeyEvent *event);

	class Info
	{
	public:
		Info(const QString &text, const QString &shortcut);
		Info(const QString &text, const std::initializer_list<QString> shortcuts);

		QString text() const;
		QString shortcut() const;
		QString tooltip() const;
		QString menuText() const;

		// For 'Keyboard shortcuts...' table:
		QString name() const; // without '&' and/or '...'
		QString shortcuts() const;

	private:
		const QString m_text;
		const QString m_tooltip;
		const QList<QString> m_shortcuts;
	};
	// Project keys:

	static Info newProject();
	static Info newProjectFromFolder();
	static Info openProject();
	static Info exit();

	// Edit keys:

	static Info refresh();
	static Info fullRefresh();
	static Info findSymbol();
	static Info executeFindSymbol();
	static Info findText();
	static Info nextReference();
	static Info previousReference();
	static Info nextLocalReference();
	static Info previousLocalReference();
	static Info customTrailDialog();
	static Info toOverview();
	static Info saveGraphAsImage();
	static Info saveAsImage();
	static Info copyToClipboard();

	static Info preferences();

	// View keys:

	static Info newTab();
	static Info closeTab();
	static Info selectNextTab();
	static Info selectPreviousTab();
	static Info largerFont();
	static Info smallerFont();
	static Info resetFontSize();

	static Info switchGraphCodeFocus();
	static Info moveCodeFocus();
	static Info moveReferenceFocus();
	static Info activateLocation();
	static Info activateLocationNewTab();
	static Info scrollCodeArea();
	static Info moveNodeFocus();
	static Info moveEdgeFocus();
	static Info activateNodeEdge();
	static Info expandOrCollapseNode();
	static Info activateNodeNewTab();
	static Info scrollGraphArea();
	static Info refreshUI();

	static Info zoomIn();
	static Info zoomOut();
	static Info resetZoom();

	// History keys:

	static Info back();
	static Info forward();

	// Bookmark keys:

	static Info bookmarkActiveSymbol();
	static Info bookmarkManager();
	static Info activateBookmark();
	static Info bookmarkNode();

	// Screen search keys:

	static Info findOnScreen();
	static Info screenSearchNext();
	static Info screenSearchPrevious();
	static Info screenSearchClose();

	// 'Help' keys:

	static Info showKeyboardShortcuts();
	static Info showLegend();

	// Miscellaneous:

	static Info textEncoding();

	// Pseudo mouse keys:

	static Info zoomInWithMouse();
	static Info zoomOutWithMouse();
	static Info expandNodeWithMouse();
	static Info collapseNodeWithMouse();
	static Info showInIDEWithMouse();
	static Info openInNewTabWithMouse();
	static Info showDefinitionWithMouse();
	static Info hideNodeWithMouse();
	static Info hideEdgeWithMouse();
};

#endif
