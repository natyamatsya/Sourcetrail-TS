#ifndef QT_LINE_GLYPH_H
#define QT_LINE_GLYPH_H

#include <QColor>
#include <QIcon>
#include <QPointF>
#include <QString>

class QPainter;

// Sourcetrail's line icons, one system for every view: drawings (.svg) on a 16 px grid with a
// 1.5 px stroke, round caps and joins, in black. The shared ones are in resources/glyphs, the
// views' own beside their style sheets. Drawn here, they take any colour, which a colour
// scheme's tokens give them; a new icon is a new drawing, not new painting code.
//
// Every icon button shows its glyph the same way: faint at rest where it waits for the mouse
// (a header's buttons, the graph's toolbar), full under the mouse, dimmed while disabled, and,
// where no token gives a highlight, on one in the glyph's colour, rounded as the controls are.
namespace QtLineGlyph
{
// The grid, and the size a glyph is drawn at (logical pixels).
inline constexpr int size{16};

inline constexpr qreal faintOpacity{0.5};
inline constexpr qreal disabledOpacity{0.35};
inline constexpr qreal hoverAlpha{0.13};
inline constexpr qreal pressedAlpha{0.24};

// The glyph at path in color, for screens of one to three pixels per point; with a
// disabledColor, in that colour while disabled.
QIcon icon(const QString& path, const QColor& color, const QColor& disabledColor = {});

// Draws the glyph at path in color, centred on center, at its size.
void draw(QPainter& painter, const QString& path, const QPointF& center, const QColor& color);

// color with its alpha scaled by alpha: a highlight in the glyph's colour.
QColor withAlpha(QColor color, qreal alpha);
}	 // namespace QtLineGlyph

#endif	  // QT_LINE_GLYPH_H
