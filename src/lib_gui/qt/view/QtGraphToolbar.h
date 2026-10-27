#ifndef QT_GRAPH_TOOLBAR_H
#define QT_GRAPH_TOOLBAR_H

#include <QColor>
#include <QFrame>
#include <QLineF>

#include <initializer_list>
#include <vector>

// The graph view's controls: one slim upright pill floating over the graph, its controls
// stacked in sections divided by hairlines. It is opaque, a graph scrolled under it hidden,
// and its controls are faint until the mouse is over it. Where the view is too short for a single column, the sections
// wrap into further columns. The colours come from graph_view.css as qproperty-* values, so
// they follow the colour scheme.
class QtGraphToolbar: public QFrame
{
	Q_OBJECT
	Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
	Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor)
	Q_PROPERTY(QColor separatorColor READ separatorColor WRITE setSeparatorColor)

public:
	// Width of a column of controls (a button is that square), and the room around them
	// (logical pixels).
	static constexpr int controlSize{24};
	static constexpr int padding{4};

	explicit QtGraphToolbar(QWidget* parent);

	QColor backgroundColor() const;
	void setBackgroundColor(const QColor& color);
	QColor borderColor() const;
	void setBorderColor(const QColor& color);
	QColor separatorColor() const;
	void setSeparatorColor(const QColor& color);

	// Adds a section below the others: the controls one above the other. A control hidden takes
	// no room, and a section with none showing leaves the toolbar, separator and all.
	QWidget& addSection(std::initializer_list<QWidget*> controls);

	// Lays the sections out within height, wrapping into columns where one does not fit.
	void fitHeight(int height);

signals:
	// The sections have moved or changed size (mapTo() them for where they are now).
	void laidOut();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	void layOut();
	void setFirm(bool firm);

	std::vector<QWidget*> m_sections;
	std::vector<QLineF> m_separators;
	int m_height{QWIDGETSIZE_MAX};

	bool m_firm{false};

	QColor m_backgroundColor{0xF7, 0xF7, 0xF8};
	QColor m_borderColor{0xD2, 0xD4, 0xD9};
	QColor m_separatorColor{0xDC, 0xDE, 0xE2};
};

#endif	  // QT_GRAPH_TOOLBAR_H
