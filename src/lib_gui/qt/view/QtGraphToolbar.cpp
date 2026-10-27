#include "QtGraphToolbar.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QVBoxLayout>
#include <QPainter>

#include <algorithm>

namespace
{
// Room between a separator and the sections either side of it.
constexpr int separatorGap{4};
// Space between the controls of a section.
constexpr int controlSpacing{2};
// The pill is fully rounded: a single column is a stadium; more columns keep its ends' curve.
constexpr qreal cornerRadius{QtGraphToolbar::controlSize / 2.0 + QtGraphToolbar::padding};

// How strong the controls are at rest; the mouse over the toolbar brings them up to full.
constexpr qreal restingOpacity{0.7};

bool showsAnyControl(const QWidget& section)
{
	const QList<QWidget*> controls{section.findChildren<QWidget*>(Qt::FindDirectChildrenOnly)};
	return std::ranges::any_of(controls, [](const QWidget* control) { return !control->isHidden(); });
}
}	 // namespace

QtGraphToolbar::QtGraphToolbar(QWidget* parent): QFrame(parent)
{
	setObjectName(QStringLiteral("graph_toolbar"));
	setAttribute(Qt::WA_Hover);
	setFrameShape(QFrame::NoFrame);
}

QColor QtGraphToolbar::backgroundColor() const
{
	return m_backgroundColor;
}

void QtGraphToolbar::setBackgroundColor(const QColor& color)
{
	m_backgroundColor = color;
	update();
}

QColor QtGraphToolbar::borderColor() const
{
	return m_borderColor;
}

void QtGraphToolbar::setBorderColor(const QColor& color)
{
	m_borderColor = color;
	update();
}

QColor QtGraphToolbar::separatorColor() const
{
	return m_separatorColor;
}

void QtGraphToolbar::setSeparatorColor(const QColor& color)
{
	m_separatorColor = color;
	update();
}

QWidget& QtGraphToolbar::addSection(std::initializer_list<QWidget*> controls)
{
	QWidget* section{new QWidget(this)};
	section->setObjectName(QStringLiteral("graph_toolbar_section"));

	// Faint at rest (see setFirm). The pill itself is not faded: it is opaque, so the graph's
	// lines and labels do not show through the controls.
	QGraphicsOpacityEffect* opacity{new QGraphicsOpacityEffect(section)};
	opacity->setOpacity(m_firm ? 1.0 : restingOpacity);
	section->setGraphicsEffect(opacity);

	QVBoxLayout* layout{new QVBoxLayout(section)};
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(controlSpacing);
	for (QWidget* control: controls)
	{
		layout->addWidget(control);
		control->installEventFilter(this);
	}

	m_sections.push_back(section);
	layOut();
	return *section;
}

void QtGraphToolbar::fitHeight(int height)
{
	if (height != m_height)
	{
		m_height = height;
		layOut();
	}
}

bool QtGraphToolbar::eventFilter(QObject* watched, QEvent* event)
{
	// A control shown or hidden changes its section's height, and maybe whether it shows at all.
	if (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent)
	{
		QMetaObject::invokeMethod(this, &QtGraphToolbar::layOut, Qt::QueuedConnection);
	}
	return QFrame::eventFilter(watched, event);
}

void QtGraphToolbar::paintEvent(QPaintEvent* /*event*/)
{
	QPainter painter{this};
	painter.setRenderHint(QPainter::Antialiasing);

	const QRectF pill{QRectF{rect()}.adjusted(0.5, 0.5, -0.5, -0.5)};
	painter.setPen(QPen{m_borderColor, 1.0});
	painter.setBrush(m_backgroundColor);
	painter.drawRoundedRect(pill, cornerRadius - 0.5, cornerRadius - 0.5);

	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(QPen{m_separatorColor, 1.0});
	for (const QLineF& separator: m_separators)
	{
		painter.drawLine(separator);
	}
}

void QtGraphToolbar::enterEvent(QEnterEvent* event)
{
	QFrame::enterEvent(event);
	setFirm(true);
}

void QtGraphToolbar::leaveEvent(QEvent* event)
{
	QFrame::leaveEvent(event);
	setFirm(false);
}

void QtGraphToolbar::layOut()
{
	const int bottom{std::max(m_height, controlSize + 2 * padding) - padding};

	m_separators.clear();
	int x{padding};
	int y{padding};
	int lowest{0};
	bool columnStarted{false};

	for (QWidget* section: m_sections)
	{
		const bool shown{showsAnyControl(*section)};
		section->setVisible(shown);
		if (!shown)
		{
			continue;
		}

		const int height{section->sizeHint().height()};
		if (columnStarted)
		{
			const int separatorY{y + separatorGap};
			if (separatorY + 1 + separatorGap + height > bottom)
			{
				x += controlSize + padding;
				y = padding;
			}
			else
			{
				m_separators.emplace_back(x + 5, separatorY, x + controlSize - 6, separatorY);
				y = separatorY + 1 + separatorGap;
			}
		}

		section->setGeometry(x, y, controlSize, height);
		y += height;
		lowest = std::max(lowest, y);
		columnStarted = true;
	}

	resize(x + controlSize + padding, lowest + padding);
	update();
	emit laidOut();
}

void QtGraphToolbar::setFirm(bool firm)
{
	m_firm = firm;
	for (QWidget* section: m_sections)
	{
		if (QGraphicsOpacityEffect* opacity = qobject_cast<QGraphicsOpacityEffect*>(section->graphicsEffect()))
		{
			opacity->setOpacity(firm ? 1.0 : restingOpacity);
		}
	}
	update();
}
