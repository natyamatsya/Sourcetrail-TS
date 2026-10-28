#ifndef QT_GLYPH_BUTTON_H
#define QT_GLYPH_BUTTON_H

#include <QColor>
#include <QPushButton>

// A small round button showing a line glyph (QtLineGlyph), faint until the mouse is over it or
// it is emphasized, e.g. as the current tab's.
// The style sheet colours it through qproperty-glyphColor; its size comes from the style
// sheet too.
class QtGlyphButton: public QPushButton
{
	Q_OBJECT
	Q_PROPERTY(QColor glyphColor READ glyphColor WRITE setGlyphColor)

public:
	enum class Glyph
	{
		Close,
		Add
	};

	explicit QtGlyphButton(Glyph glyph, QWidget* parent = nullptr);

	QColor glyphColor() const;
	void setGlyphColor(const QColor& color);

	void setEmphasized(bool emphasized);

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	const Glyph m_glyph;
	QColor m_glyphColor{0x5F, 0x63, 0x6B};
	bool m_emphasized{false};
};

#endif	  // QT_GLYPH_BUTTON_H
