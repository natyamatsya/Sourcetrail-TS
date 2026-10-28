#include "QtCodeGlyphButton.h"

#include "QtLineGlyph.h"
#include "QtResources.h"

#include <QEnterEvent>
#include <QPainter>

QString codeGlyphPath(QtCodeGlyph glyph)
{
	const char* path{QtResources::GLYPH_EDIT};
	switch (glyph)
	{
		case QtCodeGlyph::ChevronUp:
			path = QtResources::GLYPH_CHEVRON_UP;
			break;
		case QtCodeGlyph::ChevronDown:
			path = QtResources::GLYPH_CHEVRON_DOWN;
			break;
		case QtCodeGlyph::ChevronRight:
			path = QtResources::GLYPH_CHEVRON_RIGHT;
			break;
		case QtCodeGlyph::Maximize:
			path = QtResources::GLYPH_UNFOLD;
			break;
		case QtCodeGlyph::Snippets:
			path = QtResources::GLYPH_SNIPPETS;
			break;
		case QtCodeGlyph::File:
			path = QtResources::GRAPH_VIEW_FILE;	// the graph's file, as everywhere
			break;
		case QtCodeGlyph::FileIncomplete:
			path = QtResources::GLYPH_FILE_INCOMPLETE;
			break;
		case QtCodeGlyph::Edit:
			path = QtResources::GLYPH_EDIT;
			break;
	}
	return QString::fromUtf8(path);
}

QIcon codeGlyphIcon(QtCodeGlyph glyph, const QColor& color)
{
	return QtLineGlyph::icon(codeGlyphPath(glyph), color, color);
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
		painter.setOpacity(QtLineGlyph::disabledOpacity);
	}
	else if (m_faintHost != nullptr && !underMouse() && !m_faintHost->underMouse())
	{
		painter.setOpacity(QtLineGlyph::faintOpacity);
	}

	const QColor color{isChecked() && m_checkedGlyphColor.isValid() ? m_checkedGlyphColor : m_glyphColor};
	QtLineGlyph::draw(painter, codeGlyphPath(m_glyph), QRectF{rect()}.center(), color);
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
