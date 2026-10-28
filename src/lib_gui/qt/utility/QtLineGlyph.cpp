#include "QtLineGlyph.h"

#include <QHash>
#include <QImage>
#include <QPaintDevice>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int maxScale{3};

// The glyph at path drawn in black at scale pixels per point, kept for the next time.
const QImage& mask(const QString& path, int scale)
{
	static QHash<QString, QImage> masks;

	const QString key{path + QLatin1Char('@') + QString::number(scale)};
	auto it{masks.find(key)};
	if (it == masks.end())
	{
		const int pixels{QtLineGlyph::size * scale};
		QImage image{pixels, pixels, QImage::Format_ARGB32_Premultiplied};
		image.fill(Qt::transparent);
		{
			QPainter painter{&image};
			painter.setRenderHint(QPainter::Antialiasing);
			if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
			{
				QSvgRenderer{path}.render(&painter, QRectF{0, 0, static_cast<qreal>(pixels), static_cast<qreal>(pixels)});
			}
			else
			{
				painter.setRenderHint(QPainter::SmoothPixmapTransform);
				painter.drawImage(QRect{0, 0, pixels, pixels}, QImage{path});
			}
		}
		it = masks.insert(key, image);
	}
	return *it;
}

QPixmap tinted(const QString& path, int scale, const QColor& color)
{
	QImage image{mask(path, scale)};
	{
		QPainter painter{&image};
		painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
		painter.fillRect(image.rect(), color);
	}
	image.setDevicePixelRatio(scale);
	return QPixmap::fromImage(image);
}
}	 // namespace

QIcon QtLineGlyph::icon(const QString& path, const QColor& color, const QColor& disabledColor)
{
	QIcon icon;
	for (int scale{1}; scale <= maxScale; ++scale)
	{
		icon.addPixmap(tinted(path, scale, color), QIcon::Normal);
		if (disabledColor.isValid())
		{
			icon.addPixmap(tinted(path, scale, disabledColor), QIcon::Disabled);
		}
	}
	return icon;
}

void QtLineGlyph::draw(QPainter& painter, const QString& path, const QPointF& center, const QColor& color)
{
	const qreal ratio{painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0};
	const int scale{std::clamp(static_cast<int>(std::ceil(ratio)), 1, maxScale)};
	const QPointF topLeft{center - QPointF{size / 2.0, size / 2.0}};
	painter.drawPixmap(QPointF{std::round(topLeft.x()), std::round(topLeft.y())}, tinted(path, scale, color));
}

QColor QtLineGlyph::withAlpha(QColor color, qreal alpha)
{
	color.setAlphaF(color.alphaF() * alpha);
	return color;
}
