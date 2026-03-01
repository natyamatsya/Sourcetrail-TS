#ifndef QT_LINE_ITEM_BASE_H
#define QT_LINE_ITEM_BASE_H

#include <QGraphicsItem>

#include "Vector4.h"

#include "GraphViewStyle.h"

class QtLineItemBase: public QGraphicsLineItem
{
public:
	enum class Route
	{
		ROUTE_ANY,
		ROUTE_HORIZONTAL,
		ROUTE_VERTICAL
	};

	QtLineItemBase(QGraphicsItem* parent);
	~QtLineItemBase() override;

	void updateLine(
		const Vec4i& ownerRect,
		const Vec4i& targetRect,
		const Vec4i& ownerParentRect,
		const Vec4i& targetParentRect,
		const GraphViewStyle::EdgeStyle& style,
		size_t weight,
		bool showArrow);

	void setRoute(Route route);

	void setOnFront(bool front);
	void setOnBack(bool back);
	void setEarlyBend(bool earlyBend);

protected:
	QPolygon getPath() const;
	static int getDirection(QPointF a, QPointF b);

	QRectF getArrowBoundingRect(const QPolygon& poly) const;
	void drawArrow(const QPolygon& poly, QPainterPath* path, QPainterPath* arrowPath = nullptr) const;

	static void getPivotPoints(Vec2f* p, const Vec4i& in, const Vec4i& out, int offset, bool target);

	GraphViewStyle::EdgeStyle m_style;
	bool m_showArrow = true;

	bool m_onFront = false;
	bool m_onBack = false;
	bool m_earlyBend = false;

	Route m_route = Route::ROUTE_ANY;

private:
	Vec4i m_ownerRect;
	Vec4i m_targetRect;

	Vec4i m_ownerParentRect;
	Vec4i m_targetParentRect;

	mutable QPolygon m_polygon;
};

#endif	  // QT_LINE_ITEM_BASE_H
