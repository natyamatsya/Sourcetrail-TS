#include "QtDockChrome.h"

#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>

#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/Separator.h>
#include <kddockwidgets/core/SideBar.h>
#include <kddockwidgets/core/TitleBar.h>
#include <kddockwidgets/qtcommon/View.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
// Dynamic properties: a header's hover state for its buttons, and the protocol between a view
// offering its tabs and the header hosting them (see QtDockChrome::offerTabsToDockHeader).
constexpr char headerHoveredProperty[]{"dockHeaderHovered"};
constexpr char tabsOfferedProperty[]{"dockHeaderTabsOffered"};
constexpr char tabsHostProperty[]{"dockHeaderTabsHost"};
constexpr char tabsRevisionProperty[]{"dockHeaderTabsRevision"};
// And between a page offering a bar and the header showing it (QtDockChrome::offerBarToDockHeader).
constexpr char barProperty[]{"dockHeaderBar"};
constexpr char barHolderProperty[]{"dockHeaderBarHolder"};
constexpr char barSizePolicyProperty[]{"dockHeaderBarSizePolicy"};

constexpr qreal glyphSize{12};

void setHeaderHovered(QWidget& header, bool hovered)
{
	if (header.property(headerHoveredProperty).toBool() == hovered)
	{
		return;
	}
	header.setProperty(headerHoveredProperty, hovered);
	for (QtDockHeaderButton* button: header.findChildren<QtDockHeaderButton*>())
	{
		button->update();
	}
}

QColor withAlpha(QColor color, qreal alpha)
{
	color.setAlphaF(color.alphaF() * alpha);
	return color;
}

// While a grip is pressed, the cursor is a closed hand, wherever the drag takes it. Ends with
// the next mouse release anywhere, or when the application loses the mouse.
class ClosedHandWhileDragging: public QObject
{
public:
	static void start()
	{
		QGuiApplication::setOverrideCursor(Qt::ClosedHandCursor);
		qApp->installEventFilter(new ClosedHandWhileDragging(qApp));
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		switch (event->type())
		{
			case QEvent::MouseButtonRelease:
			case QEvent::NonClientAreaMouseButtonRelease:
			case QEvent::ApplicationDeactivate:
				qApp->removeEventFilter(this);
				QGuiApplication::restoreOverrideCursor();
				deleteLater();
				break;
			default:
				break;
		}
		return QObject::eventFilter(watched, event);
	}

private:
	using QObject::QObject;
};

// The rounded corners and the hairline border of a card, painted over its content, which
// paints square: the four corners outside the rounded rectangle in the canvas colour, then the
// border. It lets the mouse through. Its mask is its rim, the border plus the corners, so Qt
// knows that it covers the content only in the corners (the card keeps the content off the
// border): a view scrolling under it keeps Qt's fast scrolling, which copies what it can of the
// old picture, unless the view reaches into a corner.
class QtDockCardFrame: public QWidget
{
public:
	explicit QtDockCardFrame(QtDockCard& card)
		: QWidget(&card)
		, m_card(card)
	{
		setObjectName(QStringLiteral("dock_card_frame"));
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_NoSystemBackground);
		setFocusPolicy(Qt::NoFocus);
		card.installEventFilter(this);
		setGeometry(card.rect());
		raise();
	}

	void invalidate()
	{
		m_shapeSize = {};
		updateShape();
		update();
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched == &m_card && event->type() == QEvent::Resize)
		{
			setGeometry(m_card.rect());
			updateShape();
		}
		return QWidget::eventFilter(watched, event);
	}

	void paintEvent(QPaintEvent* event) override
	{
		updateShape();
		if (!event->region().intersects(m_rim))
		{
			return;
		}

		if (!m_card.hasBorder())
		{
			return;
		}

		QPainter painter{this};
		painter.setRenderHint(QPainter::Antialiasing);
		painter.fillPath(m_outside, m_card.canvasColor());

		const QRectF edge{QRectF{rect()}.adjusted(0.5, 0.5, -0.5, -0.5)};
		const qreal radius{m_card.effectiveRadius()};
		painter.setPen(QPen{m_card.borderColor(), 1.0});
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(edge, radius, radius);
	}

private:
	void updateShape()
	{
		const qreal radius{m_card.effectiveRadius()};
		if (m_shapeSize == size() && m_shapeRadius == radius)
		{
			return;
		}
		m_shapeSize = size();
		m_shapeRadius = radius;

		const QRectF bounds{rect()};
		QPainterPath card;
		card.addRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
		QPainterPath outside;
		outside.addRect(bounds);
		m_outside = outside.subtracted(card);

		// The border along the edges, plus the corner squares the rounding reaches into.
		const QRect all{rect()};
		const int corner{static_cast<int>(std::ceil(radius)) + 1};
		m_rim = QRegion{all} - QRegion{all.adjusted(1, 1, -1, -1)};
		m_rim += QRegion{all.left(), all.top(), corner, corner};
		m_rim += QRegion{all.right() - corner + 1, all.top(), corner, corner};
		m_rim += QRegion{all.left(), all.bottom() - corner + 1, corner, corner};
		m_rim += QRegion{all.right() - corner + 1, all.bottom() - corner + 1, corner, corner};
		setMask(m_rim);
	}

	QtDockCard& m_card;
	QSize m_shapeSize;
	qreal m_shapeRadius{-1};
	QPainterPath m_outside;
	QRegion m_rim;
};

// The header buttons' glyphs, drawn on a 12 px grid in black: the buttons use them as a mask
// and paint them in their glyph colour.
QIcon drawGlyph(KDDockWidgets::TitleBarButtonType type)
{
	using KDDockWidgets::TitleBarButtonType;

	QIcon icon;
	for (const qreal scale: {1.0, 2.0, 3.0})
	{
		const int pixels{static_cast<int>(glyphSize * scale)};
		QPixmap pixmap{pixels, pixels};
		pixmap.setDevicePixelRatio(scale);
		pixmap.fill(Qt::transparent);

		QPainter painter{&pixmap};
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(QPen{Qt::black, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
		painter.setBrush(Qt::NoBrush);

		switch (type)
		{
			case TitleBarButtonType::Close:
				painter.drawLine(QPointF{3, 3}, QPointF{9, 9});
				painter.drawLine(QPointF{9, 3}, QPointF{3, 9});
				break;
			case TitleBarButtonType::Float:
			case TitleBarButtonType::Normal:
			{
				// A window with an arrow out of its corner.
				QPainterPath box;
				box.moveTo(9.5, 7);
				box.lineTo(9.5, 9.5);
				box.lineTo(2.5, 9.5);
				box.lineTo(2.5, 2.5);
				box.lineTo(5, 2.5);
				painter.drawPath(box);
				painter.drawLine(QPointF{6, 6}, QPointF{10, 2});
				painter.drawPolyline(std::array{QPointF{7, 2}, QPointF{10, 2}, QPointF{10, 5}}.data(), 3);
				break;
			}
			case TitleBarButtonType::Maximize:
				painter.drawRoundedRect(QRectF{2.5, 2.5, 7, 7}, 1, 1);
				break;
			case TitleBarButtonType::Minimize:
				painter.drawLine(QPointF{3, 8.5}, QPointF{9, 8.5});
				break;
			case TitleBarButtonType::AutoHide:
				// Off to the side bar: an arrow towards an edge.
				painter.drawLine(QPointF{2.5, 2}, QPointF{2.5, 10});
				painter.drawLine(QPointF{10, 6}, QPointF{5, 6});
				painter.drawPolyline(std::array{QPointF{7.5, 3.5}, QPointF{5, 6}, QPointF{7.5, 8.5}}.data(), 3);
				break;
			case TitleBarButtonType::UnautoHide:
				// Back from the side bar: an arrow away from the edge.
				painter.drawLine(QPointF{2.5, 2}, QPointF{2.5, 10});
				painter.drawLine(QPointF{5, 6}, QPointF{10, 6});
				painter.drawPolyline(std::array{QPointF{7.5, 3.5}, QPointF{10, 6}, QPointF{7.5, 8.5}}.data(), 3);
				break;
			case TitleBarButtonType::AllTitleBarButtonTypes:
				break;
		}
		painter.end();
		icon.addPixmap(pixmap);
	}
	return icon;
}

QTabWidget* offeredTabs(KDDockWidgets::Core::DockWidget* dockWidget)
{
	if (dockWidget == nullptr)
	{
		return nullptr;
	}
	const QWidget* dock{KDDockWidgets::QtCommon::View_qt::asQWidget(dockWidget->view())};
	if (dock == nullptr)
	{
		return nullptr;
	}
	for (QTabWidget* tabs: dock->findChildren<QTabWidget*>())
	{
		if (tabs->property(tabsOfferedProperty).toBool())
		{
			return tabs;
		}
	}
	return nullptr;
}

QWidget* offeredBar(const QWidget& page)
{
	return qobject_cast<QWidget*>(page.property(barProperty).value<QObject*>());
}

QWidget* barHolder(const QWidget& bar)
{
	return qobject_cast<QWidget*>(bar.property(barHolderProperty).value<QObject*>());
}
}	 // namespace

// QtDockChrome

void QtDockChrome::offerTabsToDockHeader(QTabWidget& tabs)
{
	tabs.setProperty(tabsOfferedProperty, true);
}

bool QtDockChrome::tabsInDockHeader(const QTabWidget& tabs)
{
	return tabs.property(tabsHostProperty).isValid();
}

void QtDockChrome::dockHeaderTabsChanged(QTabWidget& tabs)
{
	tabs.setProperty(tabsRevisionProperty, tabs.property(tabsRevisionProperty).toInt() + 1);
}

void QtDockChrome::offerBarToDockHeader(QWidget& page, QWidget& bar)
{
	QWidget* const holder{bar.parentWidget()};
	page.setProperty(barProperty, QVariant::fromValue(static_cast<QObject*>(&bar)));
	bar.setProperty(barHolderProperty, QVariant::fromValue(static_cast<QObject*>(holder)));
	if (holder != nullptr)
	{
		// In the header, the bar outlives its page otherwise.
		QObject::connect(holder, &QObject::destroyed, &bar, &QObject::deleteLater);
	}
}

// QtDockGrip

QtDockGrip::QtDockGrip(QWidget* parent): QWidget(parent)
{
	setObjectName(QStringLiteral("dock_grip"));
	setCursor(Qt::OpenHandCursor);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	setToolTip(tr("Drag to move"));
}

QColor QtDockGrip::dotColor() const
{
	return m_dotColor;
}

void QtDockGrip::setDotColor(const QColor& color)
{
	m_dotColor = color;
	update();
}

QColor QtDockGrip::dotHoverColor() const
{
	return m_dotHoverColor;
}

void QtDockGrip::setDotHoverColor(const QColor& color)
{
	m_dotHoverColor = color;
	update();
}

QSize QtDockGrip::sizeHint() const
{
	return {14, 20};
}

void QtDockGrip::paintEvent(QPaintEvent* /*event*/)
{
	QPainter painter{this};
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(underMouse() ? m_dotHoverColor : m_dotColor);

	// Two columns of three dots, 2 px each, centred on the pixel grid.
	const QPoint centre{width() / 2, height() / 2};
	for (const int column: {-2, 2})
	{
		for (const int row: {-4, 0, 4})
		{
			painter.drawEllipse(QRectF{QPointF(centre + QPoint{column, row}) - QPointF{1, 1}, QSizeF{2, 2}});
		}
	}
}

void QtDockGrip::enterEvent(QEnterEvent* event)
{
	QWidget::enterEvent(event);
	update();
}

void QtDockGrip::leaveEvent(QEvent* event)
{
	QWidget::leaveEvent(event);
	update();
}

void QtDockGrip::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		ClosedHandWhileDragging::start();
	}
	// The header the grip sits on starts the drag: let the press through to it.
	event->ignore();
}

// QtDockCard

QtDockCard::QtDockCard(KDDockWidgets::Core::Group* controller, QWidget* parent): Group(controller, parent)
{
	setObjectName(QStringLiteral("dock_card"));
}

void QtDockCard::init()
{
	Group::init();

	// The content stays off the border, which the frame paints.
	if (QLayout* content{layout()})
	{
		content->setContentsMargins(1, 1, 1, 1);
	}
	m_frame = new QtDockCardFrame(*this);
	m_frame->show();
}

QColor QtDockCard::cardColor() const
{
	return m_cardColor;
}

void QtDockCard::setCardColor(const QColor& color)
{
	m_cardColor = color;
	update();
}

QColor QtDockCard::canvasColor() const
{
	return m_canvasColor;
}

void QtDockCard::setCanvasColor(const QColor& color)
{
	m_canvasColor = color;
	if (m_frame != nullptr)
	{
		m_frame->update();
	}
}

QColor QtDockCard::borderColor() const
{
	return m_borderColor;
}

void QtDockCard::setBorderColor(const QColor& color)
{
	m_borderColor = color;
	if (m_frame != nullptr)
	{
		m_frame->update();
	}
}

qreal QtDockCard::radius() const
{
	return m_radius;
}

void QtDockCard::setRadius(qreal radius)
{
	m_radius = radius;
	if (m_frame != nullptr)
	{
		static_cast<QtDockCardFrame*>(m_frame)->invalidate();
	}
}

qreal QtDockCard::effectiveRadius() const
{
	if (inDtor() || m_group->isOverlayed() || m_group->isFloating())
	{
		return 0;
	}
	return m_radius;
}

bool QtDockCard::hasBorder() const
{
	// Alone in a floating window, it is the window's content: the window has the frame.
	return !inDtor() && !m_group->isFloating();
}

bool QtDockCard::event(QEvent* event)
{
	// Keep the frame over whatever the group adds to itself.
	if (event->type() == QEvent::ChildAdded && m_frame != nullptr &&
		static_cast<QChildEvent*>(event)->child() != m_frame)
	{
		m_frame->raise();
	}
	return Group::event(event);
}

void QtDockCard::paintEvent(QPaintEvent* /*event*/)
{
	if (inDtor())
	{
		return;
	}
	QPainter painter{this};
	painter.fillRect(rect(), m_cardColor);
}

// QtDockGutter

QtDockGutter::QtDockGutter(KDDockWidgets::Core::Separator* controller, KDDockWidgets::Core::View* parent)
	: Separator(controller, parent)
	, m_separator(controller)
{
	setObjectName(QStringLiteral("dock_gutter"));
}

QColor QtDockGutter::canvasColor() const
{
	return m_canvasColor;
}

void QtDockGutter::setCanvasColor(const QColor& color)
{
	m_canvasColor = color;
	update();
}

QColor QtDockGutter::handleColor() const
{
	return m_handleColor;
}

void QtDockGutter::setHandleColor(const QColor& color)
{
	m_handleColor = color;
	update();
}

QColor QtDockGutter::handleHoverColor() const
{
	return m_handleHoverColor;
}

void QtDockGutter::setHandleHoverColor(const QColor& color)
{
	m_handleHoverColor = color;
	update();
}

void QtDockGutter::paintEvent(QPaintEvent* /*event*/)
{
	if (inDtor())
	{
		return;
	}
	QPainter painter{this};
	painter.fillRect(rect(), m_canvasColor);

	// A pill across the gutter's middle: 24 x 4 px, turned with the gutter.
	constexpr qreal length{24};
	constexpr qreal thickness{4};
	const bool horizontalGutter{m_separator->isVertical()};	   // separates items stacked vertically
	const QSizeF pill{horizontalGutter ? QSizeF{length, thickness} : QSizeF{thickness, length}};
	const QPointF centre{QRectF{rect()}.center()};
	const QRectF pillRect{centre - QPointF{pill.width() / 2, pill.height() / 2}, pill};

	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(m_pressed || underMouse() ? m_handleHoverColor : m_handleColor);
	painter.drawRoundedRect(pillRect, thickness / 2, thickness / 2);
}

void QtDockGutter::enterEvent(QEnterEvent* event)
{
	Separator::enterEvent(event);
	update();
}

void QtDockGutter::leaveEvent(QEvent* event)
{
	Separator::leaveEvent(event);
	update();
}

void QtDockGutter::mousePressEvent(QMouseEvent* event)
{
	m_pressed = event->button() == Qt::LeftButton;
	update();
	Separator::mousePressEvent(event);
}

void QtDockGutter::mouseReleaseEvent(QMouseEvent* event)
{
	m_pressed = false;
	update();
	Separator::mouseReleaseEvent(event);
}

// QtDockHeader

QtDockHeader::QtDockHeader(KDDockWidgets::Core::TitleBar* controller, KDDockWidgets::Core::View* parent)
	: TitleBar(controller, parent)
{
}

QtDockHeader::~QtDockHeader()
{
	// Give the tabs back to their view, which shows them itself again.
	releaseTabs();
}

void QtDockHeader::init()
{
	TitleBar::init();
	if (m_dockWidgetIcon == nullptr)
	{
		return;	   // a custom layout: no room for the grip
	}

	m_grip = new QtDockGrip(this);
	m_layout->insertWidget(0, m_grip);
	updateIconVisibility();

	connect(this, &TitleBar::currentDockWidgetChanged, this, &QtDockHeader::updateHostedTabs);
}

bool QtDockHeader::event(QEvent* event)
{
	switch (event->type())
	{
		case QEvent::ShowToParent:
		case QEvent::HideToParent:
			// Tabs in a hidden header could not be reached: they go back to their view.
			updateHostedTabs();
			break;
		case QEvent::Resize:
			// The page's bar stays in the header while there is room for it.
			updateHostedBar();
			break;
		case QEvent::LayoutRequest:
			// Posted when the dock's icon changes, among others.
			updateIconVisibility();
			break;
		default:
			break;
	}
	return TitleBar::event(event);
}

bool QtDockHeader::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_hostedTabs && event->type() == QEvent::DynamicPropertyChange &&
		static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName() == tabsRevisionProperty)
	{
		syncTabs();
	}
	else if (watched == m_watchedBar && event->type() == QEvent::LayoutRequest)
	{
		updateHostedBar();	  // it grew or shrank
	}
	return TitleBar::eventFilter(watched, event);
}

void QtDockHeader::paintEvent(QPaintEvent* /*event*/)
{
	if (inDtor() || titleBar() == nullptr)
	{
		return;
	}

	QPainter painter{this};
	QStyleOption option;
	option.initFrom(this);
	style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

	if (m_hostedTabs != nullptr)
	{
		return;	   // its tabs are the title
	}

	int left{6};
	for (const QWidget* before: {static_cast<QWidget*>(m_grip), static_cast<QWidget*>(m_dockWidgetIcon)})
	{
		if (before != nullptr && before->isVisible())
		{
			left = std::max(left, before->geometry().right() + 3);
		}
	}
	const int right{width() - buttonAreaWidth() - 4};
	if (right <= left)
	{
		return;
	}

	const QRect textRect{left, 0, right - left, height()};
	painter.setPen(palette().color(QPalette::WindowText));
	painter.drawText(
		textRect, Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(titleBar()->title(), Qt::ElideRight, textRect.width()));
}

void QtDockHeader::enterEvent(QEnterEvent* event)
{
	TitleBar::enterEvent(event);
	setHeaderHovered(*this, true);
}

void QtDockHeader::leaveEvent(QEvent* event)
{
	TitleBar::leaveEvent(event);
	setHeaderHovered(*this, false);
}

void QtDockHeader::updateIconVisibility()
{
	// An empty icon would still take room between the grip and the title.
	if (m_dockWidgetIcon != nullptr)
	{
		m_dockWidgetIcon->setVisible(!m_dockWidgetIcon->pixmap().isNull());
	}
}

void QtDockHeader::updateHostedTabs()
{
	if (inDtor() || titleBar() == nullptr || m_dockWidgetIcon == nullptr)
	{
		return;
	}
	QTabWidget* const offered{isHidden() ? nullptr : offeredTabs(titleBar()->currentDockWidget())};
	if (offered == m_hostedTabs)
	{
		return;
	}
	releaseTabs();
	if (offered != nullptr)
	{
		hostTabs(*offered);
	}
}

void QtDockHeader::hostTabs(QTabWidget& tabs)
{
	if (m_tabs == nullptr)
	{
		m_tabs = new QTabBar(this);
		m_tabs->setObjectName(QStringLiteral("dock_header_tabs"));
		m_tabs->setDrawBase(false);
		m_tabs->setExpanding(false);
		m_tabs->setUsesScrollButtons(true);
		m_tabs->setElideMode(Qt::ElideRight);
		m_tabs->setFocusPolicy(Qt::NoFocus);
		m_layout->insertWidget(m_layout->indexOf(m_dockWidgetIcon) + 1, m_tabs);

		connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
			if (m_hostedTabs != nullptr && index >= 0)
			{
				m_hostedTabs->setCurrentIndex(index);
			}
		});
	}

	m_hostedTabs = &tabs;
	m_hostedTabsCurrentChanged = connect(&tabs, &QTabWidget::currentChanged, m_tabs, &QTabBar::setCurrentIndex);
	m_hostedTabsPageChanged = connect(&tabs, &QTabWidget::currentChanged, this, &QtDockHeader::updateHostedBar);
	m_hostedTabsDestroyed = connect(&tabs, &QObject::destroyed, this, &QtDockHeader::releaseTabs);
	tabs.installEventFilter(this);
	syncTabs();
	m_tabs->show();

	tabs.setProperty(tabsHostProperty, QVariant::fromValue(reinterpret_cast<quintptr>(this)));
	updateHostedBar();
	update();
}

void QtDockHeader::releaseTabs()
{
	disconnect(m_hostedTabsCurrentChanged);
	disconnect(m_hostedTabsPageChanged);
	disconnect(m_hostedTabsDestroyed);
	returnBar();
	if (m_watchedBar != nullptr)
	{
		m_watchedBar->removeEventFilter(this);
	}
	m_watchedBar = nullptr;
	if (m_hostedTabs != nullptr)
	{
		m_hostedTabs->removeEventFilter(this);
		// Another header may have taken them over meanwhile (the dock moved): leave them there.
		if (m_hostedTabs->property(tabsHostProperty).value<quintptr>() == reinterpret_cast<quintptr>(this))
		{
			m_hostedTabs->setProperty(tabsHostProperty, QVariant{});
		}
	}
	m_hostedTabs = nullptr;
	if (m_tabs != nullptr)
	{
		m_tabs->hide();
	}
	update();
}

void QtDockHeader::syncTabs()
{
	if (m_tabs == nullptr || m_hostedTabs == nullptr)
	{
		return;
	}
	const QSignalBlocker blocker{m_tabs};
	while (m_tabs->count() > 0)
	{
		m_tabs->removeTab(0);
	}
	for (int index{0}; index < m_hostedTabs->count(); ++index)
	{
		m_tabs->addTab(m_hostedTabs->tabText(index));
		m_tabs->setTabToolTip(index, m_hostedTabs->tabToolTip(index));
	}
	m_tabs->setCurrentIndex(m_hostedTabs->currentIndex());
}

void QtDockHeader::updateHostedBar()
{
	if (inDtor() || m_tabs == nullptr)
	{
		return;
	}
	QWidget* const page{m_hostedTabs != nullptr ? m_hostedTabs->currentWidget() : nullptr};
	QWidget* const bar{page != nullptr ? offeredBar(*page) : nullptr};
	if (bar != m_watchedBar)
	{
		if (m_watchedBar != nullptr)
		{
			m_watchedBar->removeEventFilter(this);
		}
		m_watchedBar = bar;
		if (bar != nullptr)
		{
			bar->installEventFilter(this);
		}
	}

	QWidget* const wanted{bar != nullptr && hasRoomFor(*bar) ? bar : nullptr};
	if (wanted == m_bar)
	{
		return;
	}
	returnBar();
	if (wanted != nullptr)
	{
		takeBar(*wanted);
	}
}

bool QtDockHeader::hasRoomFor(const QWidget& bar) const
{
	// What the header shows besides the bar, and the bar with its divider, side by side.
	const int spacing{std::max(m_layout->spacing(), 0)};
	const QMargins margins{m_layout->contentsMargins()};
	int needed{margins.left() + margins.right()};
	for (int index{0}; index < m_layout->count(); ++index)
	{
		QLayoutItem* const item{m_layout->itemAt(index)};
		const QWidget* const widget{item->widget()};
		if (widget == nullptr || widget == m_bar || widget == m_barDivider || widget->isHidden())
		{
			continue;
		}
		needed += item->sizeHint().width() + spacing;
	}
	needed += 1 + spacing + bar.sizeHint().width();
	return needed <= width();
}

void QtDockHeader::takeBar(QWidget& bar)
{
	if (m_barDivider == nullptr)
	{
		m_barDivider = new QWidget(this);
		m_barDivider->setObjectName(QStringLiteral("dock_header_divider"));
		m_barDivider->setFixedSize(1, 14);
	}

	// Its width does not count towards the header's minimum: the header gives the bar back
	// when there is no room for it (hasRoomFor), rather than making the card wider.
	bar.setProperty(barSizePolicyProperty, QVariant::fromValue(bar.sizePolicy()));
	QSizePolicy policy{bar.sizePolicy()};
	policy.setHorizontalPolicy(QSizePolicy::Ignored);
	bar.setSizePolicy(policy);

	const int index{m_layout->indexOf(m_tabs) + 1};
	m_layout->insertWidget(index, m_barDivider);
	m_layout->insertWidget(index + 1, &bar, 1);	   // the room up to the buttons
	m_barDivider->show();
	bar.show();
	m_bar = &bar;

	if (QWidget* const holder{barHolder(bar)})
	{
		holder->hide();
	}
}

void QtDockHeader::returnBar()
{
	if (m_barDivider != nullptr)
	{
		m_layout->removeWidget(m_barDivider);
		m_barDivider->hide();
	}

	QWidget* const bar{m_bar};
	m_bar = nullptr;
	if (bar == nullptr)
	{
		return;
	}
	m_layout->removeWidget(bar);
	bar->setSizePolicy(bar->property(barSizePolicyProperty).value<QSizePolicy>());

	QWidget* const holder{barHolder(*bar)};
	if (holder == nullptr || holder->layout() == nullptr)
	{
		bar->hide();
		return;
	}
	holder->layout()->addWidget(bar);
	bar->show();
	holder->show();
}

// QtDockTabStack

QtDockTabStack::QtDockTabStack(KDDockWidgets::Core::Stack* controller, QWidget* parent): Stack(controller, parent)
{
	// The header colour behind the tab strip comes from the style sheet's background.
	setAttribute(Qt::WA_StyledBackground);
}

void QtDockTabStack::init()
{
	Stack::init();

	m_grip = new QtDockGrip(this);
	setCornerWidget(m_grip, Qt::TopLeftCorner);

	// Hover events for the whole stack, to light the buttons while over its tab strip.
	setAttribute(Qt::WA_Hover);
	tabBar()->installEventFilter(this);
	updateCornerWidgets();
}

bool QtDockTabStack::event(QEvent* event)
{
	switch (event->type())
	{
		case QEvent::HoverEnter:
		case QEvent::HoverMove:
		{
			const QPoint position{static_cast<QHoverEvent*>(event)->position().toPoint()};
			const QTabBar* tabs{tabBar()};
			setHeaderHovered(*this, tabs->isVisible() && position.y() >= 0 && position.y() < tabs->geometry().bottom() + 1);
			break;
		}
		case QEvent::HoverLeave:
			setHeaderHovered(*this, false);
			break;
		default:
			break;
	}
	return Stack::event(event);
}

bool QtDockTabStack::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == tabBar() && (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent))
	{
		updateCornerWidgets();
	}
	return Stack::eventFilter(watched, event);
}

void QtDockTabStack::updateCornerWidgets()
{
	// The corner widgets belong to the tab strip: without tabs, the title bar has the grip and
	// the buttons.
	const bool tabsShown{!tabBar()->isHidden()};
	for (const Qt::Corner corner: {Qt::TopLeftCorner, Qt::TopRightCorner})
	{
		if (QWidget* widget{cornerWidget(corner)})
		{
			widget->setVisible(tabsShown);
		}
	}
}

// QtDockHeaderButton

QtDockHeaderButton::QtDockHeaderButton(QWidget* parent): Button(parent)
{
	setFocusPolicy(Qt::NoFocus);
}

QColor QtDockHeaderButton::glyphColor() const
{
	return m_glyphColor;
}

void QtDockHeaderButton::setGlyphColor(const QColor& color)
{
	m_glyphColor = color;
	update();
}

QSize QtDockHeaderButton::sizeHint() const
{
	return {20, 20};
}

void QtDockHeaderButton::paintEvent(QPaintEvent* /*event*/)
{
	QPainter painter{this};
	painter.setRenderHint(QPainter::Antialiasing);

	const bool hovered{isEnabled() && underMouse()};
	if (hovered)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlpha(m_glyphColor, isDown() ? 0.24 : 0.13));
		painter.drawRoundedRect(QRectF{rect()}.adjusted(1, 1, -1, -1), 5, 5);
	}

	const qreal ratio{devicePixelRatioF()};
	const QPixmap mask{icon().pixmap(QSize{static_cast<int>(glyphSize), static_cast<int>(glyphSize)}, ratio)};
	if (mask.isNull())
	{
		return;
	}
	QPixmap glyph{mask.size()};
	glyph.setDevicePixelRatio(mask.devicePixelRatio());
	glyph.fill(Qt::transparent);
	{
		QPainter tint{&glyph};
		tint.drawPixmap(0, 0, mask);
		tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
		tint.fillRect(QRect{QPoint{}, glyph.size()}, m_glyphColor);
	}

	if (!isEnabled())
	{
		painter.setOpacity(0.3);
	}
	else if (!hovered && !headerHovered())
	{
		painter.setOpacity(0.45);
	}
	const QPointF topLeft{QRectF{rect()}.center() - QPointF{glyphSize / 2, glyphSize / 2}};
	painter.drawPixmap(QPointF{std::round(topLeft.x()), std::round(topLeft.y())}, glyph);
}

void QtDockHeaderButton::enterEvent(QEnterEvent* event)
{
	Button::enterEvent(event);
	update();
}

void QtDockHeaderButton::leaveEvent(QEvent* event)
{
	Button::leaveEvent(event);
	update();
}

bool QtDockHeaderButton::headerHovered() const
{
	for (const QWidget* widget{parentWidget()}; widget != nullptr; widget = widget->parentWidget())
	{
		const QVariant hovered{widget->property(headerHoveredProperty)};
		if (hovered.isValid())
		{
			return hovered.toBool();
		}
		if (widget->isWindow())
		{
			break;
		}
	}
	return false;
}

// QtDockSideBar

KDDockWidgets::SideBarButton* QtDockSideBar::createButton(KDDockWidgets::Core::DockWidget* dockWidget, SideBar* parent) const
{
	if (QLayout* buttons{parent->layout()})
	{
		buttons->setSpacing(4);
	}
	auto* button{new QtDockSideBarButton(dockWidget, parent, sideBar()->isVertical())};
	button->setObjectName(dockWidget->uniqueName() + QStringLiteral("_side_bar_button"));
	return button;
}

// QtDockSideBarButton

QtDockSideBarButton::QtDockSideBarButton(
	KDDockWidgets::Core::DockWidget* dockWidget, KDDockWidgets::QtWidgets::SideBar* parent, bool vertical)
	: SideBarButton(dockWidget, parent)
	, m_vertical(vertical)
{
	setAttribute(Qt::WA_Hover);
}

QColor QtDockSideBarButton::pillColor() const
{
	return m_pillColor;
}

void QtDockSideBarButton::setPillColor(const QColor& color)
{
	m_pillColor = color;
	update();
}

QColor QtDockSideBarButton::pillHoverColor() const
{
	return m_pillHoverColor;
}

void QtDockSideBarButton::setPillHoverColor(const QColor& color)
{
	m_pillHoverColor = color;
	update();
}

QColor QtDockSideBarButton::borderColor() const
{
	return m_borderColor;
}

void QtDockSideBarButton::setBorderColor(const QColor& color)
{
	m_borderColor = color;
	update();
}

QColor QtDockSideBarButton::textColor() const
{
	return m_textColor;
}

void QtDockSideBarButton::setTextColor(const QColor& color)
{
	m_textColor = color;
	update();
}

QSize QtDockSideBarButton::sizeHint() const
{
	const QSize lying{fontMetrics().horizontalAdvance(text()) + 20, fontMetrics().height() + 8};
	return m_vertical ? lying.transposed() : lying;
}

void QtDockSideBarButton::paintEvent(QPaintEvent* /*event*/)
{
	QPainter painter{this};
	painter.setRenderHint(QPainter::Antialiasing);

	// Paint it lying down; a vertical side bar's buttons are turned to read upwards.
	QRectF pill{QRectF{QPointF{}, m_vertical ? QSizeF{size().transposed()} : QSizeF{size()}}};
	if (m_vertical)
	{
		painter.translate(width(), 0);
		painter.rotate(90);
	}
	pill.adjust(0.5, 1.5, -0.5, -1.5);

	painter.setPen(QPen{m_borderColor, 1.0});
	painter.setBrush(underMouse() ? m_pillHoverColor : m_pillColor);
	painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);

	painter.setPen(m_textColor);
	painter.drawText(pill, Qt::AlignCenter, text());
}

// QtDockViewFactory

KDDockWidgets::Core::View* QtDockViewFactory::createGroup(KDDockWidgets::Core::Group* controller, KDDockWidgets::Core::View* parent) const
{
	return new QtDockCard(controller, KDDockWidgets::QtCommon::View_qt::asQWidget(parent));
}

KDDockWidgets::Core::View* QtDockViewFactory::createTitleBar(
	KDDockWidgets::Core::TitleBar* controller, KDDockWidgets::Core::View* parent) const
{
	return new QtDockHeader(controller, parent);
}

KDDockWidgets::Core::View* QtDockViewFactory::createStack(KDDockWidgets::Core::Stack* controller, KDDockWidgets::Core::View* parent) const
{
	return new QtDockTabStack(controller, KDDockWidgets::QtCommon::View_qt::asQWidget(parent));
}

KDDockWidgets::Core::View* QtDockViewFactory::createSeparator(
	KDDockWidgets::Core::Separator* controller, KDDockWidgets::Core::View* parent) const
{
	return new QtDockGutter(controller, parent);
}

KDDockWidgets::Core::View* QtDockViewFactory::createSideBar(KDDockWidgets::Core::SideBar* controller, KDDockWidgets::Core::View* parent) const
{
	return new QtDockSideBar(controller, KDDockWidgets::QtCommon::View_qt::asQWidget(parent));
}

QAbstractButton* QtDockViewFactory::createTitleBarButton(QWidget* parent, KDDockWidgets::TitleBarButtonType type) const
{
	using KDDockWidgets::TitleBarButtonType;

	if (parent == nullptr)
	{
		return nullptr;
	}

	auto* button{new QtDockHeaderButton(parent)};
	button->setIcon(iconForButtonType(type, parent->devicePixelRatioF()));

	// KDDockWidgets' names, until titleBarButtonObjectName() names them after their dock.
	switch (type)
	{
		case TitleBarButtonType::Close:
			button->setObjectName(QStringLiteral("closeButton"));
			break;
		case TitleBarButtonType::Float:
			button->setObjectName(QStringLiteral("floatButton"));
			break;
		case TitleBarButtonType::Minimize:
			button->setObjectName(QStringLiteral("minimizeButton"));
			break;
		case TitleBarButtonType::Maximize:
		case TitleBarButtonType::Normal:
			button->setObjectName(QStringLiteral("maximizeButton"));
			break;
		case TitleBarButtonType::AutoHide:
		case TitleBarButtonType::UnautoHide:
			button->setObjectName(QStringLiteral("autoHideButton"));
			break;
		case TitleBarButtonType::AllTitleBarButtonTypes:
			break;
	}
	return button;
}

QIcon QtDockViewFactory::iconForButtonType(KDDockWidgets::TitleBarButtonType type, qreal /*dpr*/) const
{
	// The glyphs come in several scales: one icon serves every screen.
	const int key{static_cast<int>(type)};
	auto glyph{m_glyphs.constFind(key)};
	if (glyph == m_glyphs.cend())
	{
		glyph = m_glyphs.insert(key, drawGlyph(type));
	}
	return *glyph;
}

QString QtDockViewFactory::titleBarButtonObjectName(
	KDDockWidgets::TitleBarButtonType type, KDDockWidgets::Core::DockWidget* dockWidget) const
{
	using KDDockWidgets::TitleBarButtonType;

	if (dockWidget == nullptr)
	{
		return {};	  // a floating window holding several groups: keep KDDW's name
	}
	QString button;
	switch (type)
	{
		case TitleBarButtonType::Close:
			button = QStringLiteral("close");
			break;
		case TitleBarButtonType::Float:
			button = QStringLiteral("float");
			break;
		case TitleBarButtonType::Minimize:
			button = QStringLiteral("minimize");
			break;
		case TitleBarButtonType::Maximize:
			button = QStringLiteral("maximize");
			break;
		case TitleBarButtonType::AutoHide:
			button = QStringLiteral("auto_hide");
			break;
		default:
			return {};
	}
	return dockWidget->uniqueName() + QLatin1Char('_') + button + QStringLiteral("_button");
}
