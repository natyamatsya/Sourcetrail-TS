#ifndef QT_GRAPHICS_VIEW_H
#define QT_GRAPHICS_VIEW_H

#include "MessageListener.h"
#include "MessageSaveAsImage.h"
#include "QtActions.h"

#include <QGraphicsView>

#include <memory>
#include <optional>

class GraphFocusHandler;
class QPushButton;
class QTimer;
class QtGraphEdge;
class QtGraphNode;
class QtSelfRefreshIconButton;

class QtGraphicsView: public QGraphicsView, public MessageListener<MessageSaveAsImage>
{
	Q_OBJECT

public:
	QtGraphicsView(GraphFocusHandler* focusHandler, QWidget* parent);

	// The zoom and legend buttons, for the graph view's toolbar.
	QtSelfRefreshIconButton& zoomInButton() const;
	QtSelfRefreshIconButton& zoomOutButton() const;
	QtSelfRefreshIconButton& legendButton() const;
	// The zoom level shows for a moment after each change, beside the zoom buttons: the middle
	// of its left edge, in this view's coordinates.
	void placeZoomState(QPoint leftCentre);
	// Keeps the graph out of a strip along the view's left edge, where the toolbar floats: the
	// viewport starts right of it, so no node is drawn under the toolbar, at any width or scroll.
	void reserveLeftEdge(int pixels);

	float getZoomFactor() const;
	void setAppZoomFactor(float appZoomFactor);

	// The scene's rect, which an exported image shows. The view scrolls and centres within it
	// widened by reservedLeft (scene units) to the left: room for what floats over the view's
	// left edge, so the graph is laid out beside it.
	void setSceneRect(const QRectF& rect, qreal reservedLeft = 0);

	QtGraphNode* getNodeAtCursorPosition() const;
	QtGraphEdge* getEdgeAtCursorPosition() const;

	// Scrolls rect into view, and then horizontally to horizontalValue if given.
	void ensureVisibleAnimated(
		const QRectF& rect, int xmargin = 50, int ymargin = 50, std::optional<int> horizontalValue = std::nullopt);

	void updateZoom(float delta);

	TabId getSchedulerId() const override
	{
		return m_tabId;
	}

protected:
	void resizeEvent(QResizeEvent* event) override;

	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

	void keyPressEvent(QKeyEvent* event) override;
	void keyReleaseEvent(QKeyEvent* event) override;

	void wheelEvent(QWheelEvent* event) override;

	void contextMenuEvent(QContextMenuEvent* event) override;

	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;

signals:
	void emptySpaceClicked();
	void resized();

	void focusIn();
	void focusOut();

private slots:
	void updateTimer();
	void stopTimer();

	void openInTab();

	QImage toQImage();
	void exportGraph();
	void copyGraph();
	void copyNodeName();

	void collapseNode();
	void expandNode();

	void showInIDE();
	void showDefinition();
	void hideNode();
	void hideEdge();
	void bookmarkNode();

	void zoomInPressed();
	void zoomOutPressed();

	void hideZoomLabel();

	static void legendClicked();

private:
	void setZoomFactor(float zoomFactor);
	void updateTransform();

	void handleMessage(MessageSaveAsImage* message) override;

	GraphFocusHandler* m_focusHandler;

	QPoint m_last;

	float m_zoomFactor;
	float m_appZoomFactor = 1.0f;

	Action m_lastAction = Action::Unknown;

	std::string m_clipboardNodeName;
	Id m_openInTabNodeId;
	Id m_hideNodeId;
	Id m_hideEdgeId;
	Id m_bookmarkNodeId;
	Id m_collapseNodeId;
	Id m_expandNodeId;

	std::shared_ptr<QTimer> m_timer;
	std::shared_ptr<QTimer> m_timerStopper;
	std::shared_ptr<QTimer> m_zoomLabelTimer;

	QAction* m_openInTabAction;

	QAction* m_copyNodeNameAction;
	QAction* m_collapseAction;
	QAction* m_expandAction;
	QAction* m_showInIDEAction;
	QAction* m_showDefinitionAction;
	QAction* m_hideNodeAction;
	QAction* m_hideEdgeAction;
	QAction* m_bookmarkNodeAction;

	QAction* m_exportGraphAction;
	QAction* m_copyGraphAction;

	QWidget* m_focusIndicator;

	QPushButton* m_zoomState;
	QtSelfRefreshIconButton* m_zoomInButton;
	QtSelfRefreshIconButton* m_zoomOutButton;

	QtSelfRefreshIconButton* m_legendButton;

	TabId m_tabId;
};

#endif	  // QT_GRAPHICS_VIEW_H
