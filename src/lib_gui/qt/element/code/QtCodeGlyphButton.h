#ifndef QT_CODE_GLYPH_BUTTON_H
#define QT_CODE_GLYPH_BUTTON_H

#include <QColor>
#include <QIcon>
#include <QPushButton>

class QPainter;

// The code view's small line icons: drawn, not loaded, so they stay crisp at any scale and take
// any colour. They share one stroke and a 16 px grid.
enum class QtCodeGlyph
{
	ChevronUp,
	ChevronDown,
	ChevronRight,
	Maximize,	   // the whole file: unfold
	Snippets,	   // the file's snippets only
	File,
	FileIncomplete,
	Edit
};

// Draws the glyph into box (any size: the 16 px grid is scaled to it) with the painter's pen
// colour.
void drawCodeGlyph(QPainter& painter, QtCodeGlyph glyph, const QRectF& box, const QColor& color);

// The glyph as an icon of size x size logical pixels, in color, for every screen scale.
QIcon codeGlyphIcon(QtCodeGlyph glyph, const QColor& color, int size);

// A flat button showing a glyph. The style sheet gives it its glyph colours
// (qproperty-glyphColor, qproperty-checkedGlyphColor) and its background, hover and checked
// states as for any QPushButton.
// It can stay faint until its host, the header line it sits on, is hovered.
class QtCodeGlyphButton: public QPushButton
{
	Q_OBJECT
	Q_PROPERTY(QColor glyphColor READ glyphColor WRITE setGlyphColor)
	Q_PROPERTY(QColor checkedGlyphColor READ checkedGlyphColor WRITE setCheckedGlyphColor)

public:
	explicit QtCodeGlyphButton(QtCodeGlyph glyph, QWidget* parent = nullptr);

	QtCodeGlyph glyph() const;
	void setGlyph(QtCodeGlyph glyph);

	QColor glyphColor() const;
	void setGlyphColor(const QColor& color);

	// The glyph colour while checked (a style sheet sets its properties once, not per state);
	// none: the glyph colour.
	QColor checkedGlyphColor() const;
	void setCheckedGlyphColor(const QColor& color);

	// Faint until host (or the button) is under the mouse; the host repaints its buttons when
	// the mouse enters or leaves it (see updateFaintButtons).
	void setFaintUntilHovered(const QWidget* host);

	// Repaints the faint buttons whose host is host.
	static void updateFaintButtons(const QWidget& host);

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	QtCodeGlyph m_glyph;
	QColor m_glyphColor{0x6F, 0x73, 0x7B};
	QColor m_checkedGlyphColor;
	const QWidget* m_faintHost{nullptr};
};

#endif	  // QT_CODE_GLYPH_BUTTON_H
