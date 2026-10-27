#include "QtGlyphButton.h"

#include <QPainter>

#include <algorithm>

namespace
{
constexpr qreal glyphSize{12};
constexpr qreal glyphStroke{1.2};

constexpr qreal faintOpacity{0.45};
constexpr qreal hoverAlpha{0.13};
constexpr qreal pressedAlpha{0.24};

QColor withAlpha(QColor color, qreal alpha)
{
	color.setAlphaF(color.alphaF() * alpha);
	return color;
}
}	 // namespace

QtGlyphButton::QtGlyphButton(Glyph glyph, QWidget* parent)
	: QPushButton(parent)
	, m_glyph{glyph}
{
	setFlat(true);
	setFocusPolicy(Qt::NoFocus);
	setCursor(Qt::ArrowCursor);
	setAttribute(Qt::WA_LayoutUsesWidgetRect);	  // fixes layouting on Mac
}

QColor QtGlyphButton::glyphColor() const
{
	return m_glyphColor;
}

void QtGlyphButton::setGlyphColor(const QColor& color)
{
	m_glyphColor = color;
	update();
}

void QtGlyphButton::setEmphasized(bool emphasized)
{
	if (m_emphasized != emphasized)
	{
		m_emphasized = emphasized;
		update();
	}
}

void QtGlyphButton::paintEvent(QPaintEvent* /*event*/)
{
	QPainter painter{this};
	painter.setRenderHint(QPainter::Antialiasing);

	const bool hovered{isEnabled() && underMouse()};
	if (hovered)
	{
		const QRectF background{QRectF{rect()}.adjusted(1, 1, -1, -1)};
		const qreal radius{std::min(background.width(), background.height()) / 2};
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlpha(m_glyphColor, isDown() ? pressedAlpha : hoverAlpha));
		painter.drawRoundedRect(background, radius, radius);
	}

	if (!isEnabled())
	{
		painter.setOpacity(0.3);
	}
	else if (!hovered && !m_emphasized)
	{
		painter.setOpacity(faintOpacity);
	}

	// The glyph on a 12px grid, centred on the button.
	painter.translate(QRectF{rect()}.center() - QPointF{glyphSize / 2, glyphSize / 2});
	painter.setPen(QPen{m_glyphColor, glyphStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
	painter.setBrush(Qt::NoBrush);
	switch (m_glyph)
	{
		case Glyph::Close:
			painter.drawLine(QPointF{3.5, 3.5}, QPointF{8.5, 8.5});
			painter.drawLine(QPointF{8.5, 3.5}, QPointF{3.5, 8.5});
			break;
		case Glyph::Add:
			painter.drawLine(QPointF{6, 2.5}, QPointF{6, 9.5});
			painter.drawLine(QPointF{2.5, 6}, QPointF{9.5, 6});
			break;
	}
}

void QtGlyphButton::enterEvent(QEnterEvent* event)
{
	QPushButton::enterEvent(event);
	update();
}

void QtGlyphButton::leaveEvent(QEvent* event)
{
	QPushButton::leaveEvent(event);
	update();
}
