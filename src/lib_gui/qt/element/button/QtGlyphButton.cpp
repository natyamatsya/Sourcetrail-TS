#include "QtGlyphButton.h"

#include "QtLineGlyph.h"
#include "QtResources.h"

#include <QPainter>

#include <algorithm>

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
		// A round highlight: the button sits in a pill (a tab) or among pills.
		const QRectF background{QRectF{rect()}.adjusted(1, 1, -1, -1)};
		const qreal radius{std::min(background.width(), background.height()) / 2};
		painter.setPen(Qt::NoPen);
		painter.setBrush(QtLineGlyph::withAlpha(m_glyphColor, isDown() ? QtLineGlyph::pressedAlpha : QtLineGlyph::hoverAlpha));
		painter.drawRoundedRect(background, radius, radius);
	}

	if (!isEnabled())
	{
		painter.setOpacity(QtLineGlyph::disabledOpacity);
	}
	else if (!hovered && !m_emphasized)
	{
		painter.setOpacity(QtLineGlyph::faintOpacity);
	}

	const char* path{m_glyph == Glyph::Close ? QtResources::GLYPH_CLOSE : QtResources::GLYPH_ADD};
	QtLineGlyph::draw(painter, QString::fromUtf8(path), QRectF{rect()}.center(), m_glyphColor);
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
