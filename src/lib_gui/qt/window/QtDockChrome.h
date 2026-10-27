#ifndef QT_DOCK_CHROME_H
#define QT_DOCK_CHROME_H

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QMetaObject>
#include <QPointer>
#include <QWidget>

#include <kddockwidgets/qtwidgets/ViewFactory.h>
#include <kddockwidgets/qtwidgets/views/FloatingWindow.h>
#include <kddockwidgets/qtwidgets/views/Group.h>
#include <kddockwidgets/qtwidgets/views/Separator.h>
#include <kddockwidgets/qtwidgets/views/SideBar.h>
#include <kddockwidgets/qtwidgets/views/Stack.h>
#include <kddockwidgets/qtwidgets/views/TitleBar.h>

class QMainWindow;
class QTabBar;
class QTabWidget;

// Sourcetrail's docking chrome, the views KDDockWidgets builds the docks from (see
// QtDockViewFactory): every dock group is a card with rounded corners on the window's canvas,
// separated from the others by gutters instead of lines. A gutter carries a small pill to
// resize by, a card's header a small grip to move the card by. Headers are slim and their
// buttons faint until the header is hovered. The colours come from main.css as qproperty-*
// values, so they follow the colour scheme.
namespace QtDockChrome
{
// Width of the gutters between the cards, and of the canvas around them (logical pixels).
inline constexpr int gutterWidth{6};

// A view whose QTabWidget can show its tabs in its dock's header instead, for example the
// Status view's "Status | Errors": offer them once. While a header hosts them,
// tabsInDockHeader() is true, and the tab widget gets a QEvent::DynamicPropertyChange whenever
// that changes: hide the tab widget's own tab bar meanwhile.
void offerTabsToDockHeader(QTabWidget& tabs);
bool tabsInDockHeader(const QTabWidget& tabs);
// Tell the hosting header that tabs were added, removed or renamed.
void dockHeaderTabsChanged(QTabWidget& tabs);

// A page of offered tabs can put a bar of its own into the header too, right of the tabs: its
// filters, say. The bar sits alone in a holder of the page's, which shows it while the header
// does not: while the header hosts the tabs, the page is current and the header has room for
// it, the bar is in the header and the holder hidden. Offer it once the bar is in its holder.
void offerBarToDockHeader(QWidget& page, QWidget& bar);
}	 // namespace QtDockChrome

// The small grip to pick a card up by: two columns of three dots, with an open hand cursor
// (a closed one while dragging). It leaves the mouse to the header it sits on, which starts
// the drag.
class QtDockGrip: public QWidget
{
	Q_OBJECT
	Q_PROPERTY(QColor dotColor READ dotColor WRITE setDotColor)
	Q_PROPERTY(QColor dotHoverColor READ dotHoverColor WRITE setDotHoverColor)

public:
	explicit QtDockGrip(QWidget* parent);

	QColor dotColor() const;
	void setDotColor(const QColor& color);
	QColor dotHoverColor() const;
	void setDotHoverColor(const QColor& color);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;

private:
	QColor m_dotColor{0xB3, 0xB7, 0xBF};
	QColor m_dotHoverColor{0x6F, 0x74, 0x7E};
};

// A dock group as a card. Its children paint square, so the rounded corners and the hairline
// border are painted over them by a frame that lets the mouse through.
class QtDockCard: public KDDockWidgets::QtWidgets::Group
{
	Q_OBJECT
	Q_PROPERTY(QColor cardColor READ cardColor WRITE setCardColor)
	Q_PROPERTY(QColor canvasColor READ canvasColor WRITE setCanvasColor)
	Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor)
	Q_PROPERTY(qreal radius READ radius WRITE setRadius)

public:
	QtDockCard(KDDockWidgets::Core::Group* controller, QWidget* parent);

	void init() override;

	QColor cardColor() const;
	void setCardColor(const QColor& color);
	QColor canvasColor() const;
	void setCanvasColor(const QColor& color);
	QColor borderColor() const;
	void setBorderColor(const QColor& color);
	qreal radius() const;
	void setRadius(qreal radius);

	// The corner radius it has where it is: none for an auto-hidden dock shown over the others,
	// whose corners would show the canvas over them, nor alone in a floating window.
	qreal effectiveRadius() const;
	bool hasBorder() const;

protected:
	bool event(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	QWidget* m_frame{nullptr};
	QColor m_cardColor{Qt::white};
	QColor m_canvasColor{0xE6, 0xE7, 0xEA};
	QColor m_borderColor{0xD2, 0xD4, 0xD9};
	qreal m_radius{8};
};

// A floating window, with a soft frame: rounded corners and a hairline border. A single card in
// it (one dock, or docks tabbed together) is the window, its header the window's; several cards
// side by side sit on a canvas, under a header row for the whole window.
//
// Where the window can be see-through (macOS, whose floating windows have no native frame, see
// QtDockChrome.cpp), its corners are: the window's content, and the system's shadow, are rounded.
// KDDockWidgets resizes a frameless window from a margin along its edges that belongs to the
// window itself, the content leaves it free: there it is all but transparent, so the window seems
// to have the content's size and is resized from just outside its edges, as a native window is.
// Elsewhere the window is opaque and square (Windows 11 rounds it itself), and its card sits on the
// canvas with rounded corners, like the cards in the main window.
class QtDockFloatingWindow: public KDDockWidgets::QtWidgets::FloatingWindow
{
	Q_OBJECT
	Q_PROPERTY(QColor canvasColor READ canvasColor WRITE setCanvasColor)
	Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor)
	Q_PROPERTY(QColor cardBorderColor READ cardBorderColor WRITE setCardBorderColor)
	Q_PROPERTY(qreal radius READ radius WRITE setRadius)

public:
	QtDockFloatingWindow(KDDockWidgets::Core::FloatingWindow* controller, QMainWindow* parent, Qt::WindowFlags windowFlags);

	QColor canvasColor() const;
	void setCanvasColor(const QColor& color);
	QColor borderColor() const;
	void setBorderColor(const QColor& color);
	QColor cardBorderColor() const;
	void setCardBorderColor(const QColor& color);
	qreal radius() const;
	void setRadius(qreal radius);

	// Whether the window's corners are see-through.
	bool isTranslucent() const;
	// Whether it holds several cards side by side, under a header row of its own.
	bool holdsSeveralCards() const;
	// What looks like the window: all of it, or, see-through, all but the margin to resize it by.
	QRect frameRect() const;
	// The single card's content (header included), which the window's frame rounds; empty while it
	// holds several cards, which round themselves.
	QRect cardRect() const;
	// The width of the margin KDDockWidgets resizes a frameless window by.
	int resizeMargin() const;

protected:
	bool event(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void updateMargins() override;

private:
	const bool m_translucent;
	bool m_severalCards{false};
	QWidget* m_frame{nullptr};
	QColor m_canvasColor{0xE6, 0xE7, 0xEA};
	QColor m_borderColor{0xC3, 0xC6, 0xCC};
	QColor m_cardBorderColor{0xD2, 0xD4, 0xD9};
	qreal m_radius{10};
};

// A gutter between cards, still the separator to drag. It shows a short pill to grab, which
// gets stronger on hover.
class QtDockGutter: public KDDockWidgets::QtWidgets::Separator
{
	Q_OBJECT
	Q_PROPERTY(QColor canvasColor READ canvasColor WRITE setCanvasColor)
	Q_PROPERTY(QColor handleColor READ handleColor WRITE setHandleColor)
	Q_PROPERTY(QColor handleHoverColor READ handleHoverColor WRITE setHandleHoverColor)

public:
	QtDockGutter(KDDockWidgets::Core::Separator* controller, KDDockWidgets::Core::View* parent);

	QColor canvasColor() const;
	void setCanvasColor(const QColor& color);
	QColor handleColor() const;
	void setHandleColor(const QColor& color);
	QColor handleHoverColor() const;
	void setHandleHoverColor(const QColor& color);

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

private:
	KDDockWidgets::Core::Separator* const m_separator;
	bool m_pressed{false};
	QColor m_canvasColor{0xE6, 0xE7, 0xEA};
	QColor m_handleColor{0xB3, 0xB7, 0xBF};
	QColor m_handleHoverColor{0x6F, 0x74, 0x7E};
};

// A card's slim header: the grip, the title, and the buttons. It shows the tabs a dock offers
// (QtDockChrome::offerTabsToDockHeader) in place of the title.
class QtDockHeader: public KDDockWidgets::QtWidgets::TitleBar
{
	Q_OBJECT

public:
	QtDockHeader(KDDockWidgets::Core::TitleBar* controller, KDDockWidgets::Core::View* parent);
	~QtDockHeader() override;

protected:
	void init() override;
	bool event(QEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	void updateIconVisibility();
	void updateHostedTabs();
	void hostTabs(QTabWidget& tabs);
	void releaseTabs();
	void syncTabs();

	// The bar of the hosted tabs' current page (QtDockChrome::offerBarToDockHeader).
	void updateHostedBar();
	bool hasRoomFor(const QWidget& bar) const;
	void takeBar(QWidget& bar);
	void returnBar();

	QtDockGrip* m_grip{nullptr};
	QTabBar* m_tabs{nullptr};
	QPointer<QTabWidget> m_hostedTabs;
	QMetaObject::Connection m_hostedTabsCurrentChanged;
	QMetaObject::Connection m_hostedTabsPageChanged;
	QMetaObject::Connection m_hostedTabsDestroyed;
	QWidget* m_barDivider{nullptr};
	QPointer<QWidget> m_bar;		   // in the header
	QPointer<QWidget> m_watchedBar;	   // the current page's, watched for its size
};

// The stack of a card's tabs. When its tabs are the header (docks tabbed together), the grip
// goes left of them, and the buttons next to them are faint until the tab strip is hovered.
class QtDockTabStack: public KDDockWidgets::QtWidgets::Stack
{
	Q_OBJECT

public:
	QtDockTabStack(KDDockWidgets::Core::Stack* controller, QWidget* parent);

	void init() override;

protected:
	bool event(QEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void updateCornerWidgets();

	QtDockGrip* m_grip{nullptr};
};

// A header button: a 12 px monochrome glyph (its icon, used as a mask), faint until its
// header is hovered, with a rounded highlight under the mouse.
class QtDockHeaderButton: public KDDockWidgets::QtWidgets::Button
{
	Q_OBJECT
	Q_PROPERTY(QColor glyphColor READ glyphColor WRITE setGlyphColor)

public:
	explicit QtDockHeaderButton(QWidget* parent);

	QColor glyphColor() const;
	void setGlyphColor(const QColor& color);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	bool headerHovered() const;

	QColor m_glyphColor{0x5F, 0x63, 0x6B};
};

// The side bar of auto-hidden docks, with pill buttons that carry only the dock's title.
class QtDockSideBar: public KDDockWidgets::QtWidgets::SideBar
{
	Q_OBJECT

public:
	using SideBar::SideBar;

protected:
	KDDockWidgets::SideBarButton* createButton(KDDockWidgets::Core::DockWidget* dockWidget, SideBar* parent) const override;
};

class QtDockSideBarButton: public KDDockWidgets::SideBarButton
{
	Q_OBJECT
	Q_PROPERTY(QColor pillColor READ pillColor WRITE setPillColor)
	Q_PROPERTY(QColor pillHoverColor READ pillHoverColor WRITE setPillHoverColor)
	Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor)
	Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor)

public:
	QtDockSideBarButton(KDDockWidgets::Core::DockWidget* dockWidget, KDDockWidgets::QtWidgets::SideBar* parent, bool vertical);

	QColor pillColor() const;
	void setPillColor(const QColor& color);
	QColor pillHoverColor() const;
	void setPillHoverColor(const QColor& color);
	QColor borderColor() const;
	void setBorderColor(const QColor& color);
	QColor textColor() const;
	void setTextColor(const QColor& color);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	const bool m_vertical;
	QColor m_pillColor{0xF7, 0xF7, 0xF8};
	QColor m_pillHoverColor{0xE3, 0xE5, 0xE9};
	QColor m_borderColor{0xD2, 0xD4, 0xD9};
	QColor m_textColor{0x5F, 0x63, 0x6B};
};

// Builds the docks from the views above. It also names the title bar and tab bar buttons after
// their dock ("Graph_close_button"), since KDDockWidgets names them by type ("closeButton"),
// alike in every dock, so an agent could not address one by objectName. In a tab group they
// act on the whole group and take the current tab's name.
class QtDockViewFactory: public KDDockWidgets::QtWidgets::ViewFactory
{
public:
	KDDockWidgets::Core::View* createGroup(KDDockWidgets::Core::Group* controller, KDDockWidgets::Core::View* parent) const override;
	KDDockWidgets::Core::View* createTitleBar(KDDockWidgets::Core::TitleBar* controller, KDDockWidgets::Core::View* parent) const override;
	KDDockWidgets::Core::View* createStack(KDDockWidgets::Core::Stack* controller, KDDockWidgets::Core::View* parent) const override;
	KDDockWidgets::Core::View* createSeparator(KDDockWidgets::Core::Separator* controller, KDDockWidgets::Core::View* parent) const override;
	KDDockWidgets::Core::View* createSideBar(KDDockWidgets::Core::SideBar* controller, KDDockWidgets::Core::View* parent) const override;
	KDDockWidgets::Core::View* createFloatingWindow(
		KDDockWidgets::Core::FloatingWindow* controller, KDDockWidgets::Core::MainWindow* parent, Qt::WindowFlags windowFlags) const override;

	QAbstractButton* createTitleBarButton(QWidget* parent, KDDockWidgets::TitleBarButtonType type) const override;
	QIcon iconForButtonType(KDDockWidgets::TitleBarButtonType type, qreal dpr) const override;
	QString titleBarButtonObjectName(KDDockWidgets::TitleBarButtonType type, KDDockWidgets::Core::DockWidget* dockWidget) const override;

private:
	mutable QHash<int, QIcon> m_glyphs;
};

#endif	  // QT_DOCK_CHROME_H
