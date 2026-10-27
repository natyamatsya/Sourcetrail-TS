#include "QtCodeGlyphButton.h"

#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <array>
#include <cmath>

namespace
{
constexpr qreal gridSize{16};
constexpr qreal strokeWidth{1.3};
constexpr int glyphSize{16};

// How a glyph looks when it is not the one to look at: disabled, or faint until hovered.
constexpr qreal disabledOpacity{0.35};
constexpr qreal faintOpacity{0.5};

void drawPolyline(QPainter& painter, const std::array<QPointF, 3>& points)
{
	painter.drawPolyline(points.data(), static_cast<int>(points.size()));
}

QPainterPath filePath()
{
	// A sheet with its top right corner folded.
	QPainterPath sheet;
	sheet.moveTo(4, 2.5);
	sheet.lineTo(9.5, 2.5);
	sheet.lineTo(12.5, 5.5);
	sheet.lineTo(12.5, 13.5);
	sheet.lineTo(4, 13.5);
	sheet.closeSubpath();
	sheet.moveTo(9.5, 2.5);
	sheet.lineTo(9.5, 5.5);
	sheet.lineTo(12.5, 5.5);
	return sheet;
}
}	 // namespace

void drawCodeGlyph(QPainter& painter, QtCodeGlyph glyph, const QRectF& box, const QColor& color)
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	painter.translate(box.topLeft());
	painter.scale(box.width() / gridSize, box.height() / gridSize);
	painter.setPen(QPen{color, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
	painter.setBrush(Qt::NoBrush);

	switch (glyph)
	{
		case QtCodeGlyph::ChevronUp:
			drawPolyline(painter, {QPointF{4.5, 10}, QPointF{8, 6.5}, QPointF{11.5, 10}});
			break;
		case QtCodeGlyph::ChevronDown:
			drawPolyline(painter, {QPointF{4.5, 6.5}, QPointF{8, 10}, QPointF{11.5, 6.5}});
			break;
		case QtCodeGlyph::ChevronRight:
			drawPolyline(painter, {QPointF{6.5, 4.5}, QPointF{10, 8}, QPointF{6.5, 11.5}});
			break;
		case QtCodeGlyph::Maximize:
			// Unfold: arrows up and down, away from a line.
			painter.drawLine(QPointF{3.5, 8}, QPointF{12.5, 8});
			painter.drawLine(QPointF{8, 2.5}, QPointF{8, 5.5});
			drawPolyline(painter, {QPointF{5.75, 4.75}, QPointF{8, 2.5}, QPointF{10.25, 4.75}});
			painter.drawLine(QPointF{8, 10.5}, QPointF{8, 13.5});
			drawPolyline(painter, {QPointF{5.75, 11.25}, QPointF{8, 13.5}, QPointF{10.25, 11.25}});
			break;
		case QtCodeGlyph::Snippets:
			painter.drawRoundedRect(QRectF{3.5, 3.5, 9, 3.5}, 1, 1);
			painter.drawRoundedRect(QRectF{3.5, 9, 9, 3.5}, 1, 1);
			break;
		case QtCodeGlyph::File:
			painter.drawPath(filePath());
			break;
		case QtCodeGlyph::FileIncomplete:
			painter.drawPath(filePath());
			painter.drawLine(QPointF{6.5, 8}, QPointF{10, 11.5});
			painter.drawLine(QPointF{10, 8}, QPointF{6.5, 11.5});
			break;
		case QtCodeGlyph::Edit:
		{
			// A pencil.
			QPainterPath pencil;
			pencil.moveTo(3.5, 12.5);
			pencil.lineTo(4, 10);
			pencil.lineTo(10.5, 3.5);
			pencil.lineTo(12.5, 5.5);
			pencil.lineTo(6, 12);
			pencil.closeSubpath();
			painter.drawPath(pencil);
			painter.drawLine(QPointF{9, 5}, QPointF{11, 7});
			break;
		}
	}

	painter.restore();
}

QIcon codeGlyphIcon(QtCodeGlyph glyph, const QColor& color, int size)
{
	QIcon icon;
	for (const qreal scale: {1.0, 2.0, 3.0})
	{
		const int pixels{static_cast<int>(std::lround(size * scale))};
		QPixmap pixmap{pixels, pixels};
		pixmap.setDevicePixelRatio(scale);
		pixmap.fill(Qt::transparent);

		QPainter painter{&pixmap};
		drawCodeGlyph(painter, glyph, QRectF{0, 0, static_cast<qreal>(size), static_cast<qreal>(size)}, color);
		painter.end();

		icon.addPixmap(pixmap, QIcon::Normal);
		icon.addPixmap(pixmap, QIcon::Disabled);
	}
	return icon;
}

QtCodeGlyphButton::QtCodeGlyphButton(QtCodeGlyph glyph, QWidget* parent): QPushButton(parent), m_glyph(glyph)
{
	setAttribute(Qt::WA_LayoutUsesWidgetRect);	  // fixes layouting on Mac
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QtCodeGlyph QtCodeGlyphButton::glyph() const
{
	return m_glyph;
}

void QtCodeGlyphButton::setGlyph(QtCodeGlyph glyph)
{
	if (m_glyph != glyph)
	{
		m_glyph = glyph;
		update();
	}
}

QColor QtCodeGlyphButton::glyphColor() const
{
	return m_glyphColor;
}

void QtCodeGlyphButton::setGlyphColor(const QColor& color)
{
	m_glyphColor = color;
	update();
}

QColor QtCodeGlyphButton::checkedGlyphColor() const
{
	return m_checkedGlyphColor;
}

void QtCodeGlyphButton::setCheckedGlyphColor(const QColor& color)
{
	m_checkedGlyphColor = color;
	update();
}

void QtCodeGlyphButton::setFaintUntilHovered(const QWidget* host)
{
	m_faintHost = host;
	update();
}

void QtCodeGlyphButton::updateFaintButtons(const QWidget& host)
{
	for (QtCodeGlyphButton* button: host.findChildren<QtCodeGlyphButton*>())
	{
		if (button->m_faintHost == &host)
		{
			button->update();
		}
	}
}

void QtCodeGlyphButton::paintEvent(QPaintEvent* event)
{
	// The style sheet's background and border, without text or icon.
	QPushButton::paintEvent(event);

	QPainter painter{this};
	if (!isEnabled())
	{
		painter.setOpacity(disabledOpacity);
	}
	else if (m_faintHost != nullptr && !underMouse() && !m_faintHost->underMouse())
	{
		painter.setOpacity(faintOpacity);
	}

	const QRectF box{QRectF{rect()}.center() - QPointF{glyphSize / 2.0, glyphSize / 2.0}, QSizeF{glyphSize, glyphSize}};
	const QColor color{isChecked() && m_checkedGlyphColor.isValid() ? m_checkedGlyphColor : m_glyphColor};
	drawCodeGlyph(painter, m_glyph, QRectF{std::round(box.x()), std::round(box.y()), box.width(), box.height()}, color);
}

void QtCodeGlyphButton::enterEvent(QEnterEvent* event)
{
	QPushButton::enterEvent(event);
	update();
}

void QtCodeGlyphButton::leaveEvent(QEvent* event)
{
	QPushButton::leaveEvent(event);
	update();
}
