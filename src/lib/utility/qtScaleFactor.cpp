#include "qtScaleFactor.h"

#include <QByteArray>

namespace utility
{
const char QT_AUTO_SCREEN_SCALE_FACTOR[] = "QT_AUTO_SCREEN_SCALE_FACTOR";

bool isQtAutoScreenScaleFactorEnabled()
{
	return qgetenv(QT_AUTO_SCREEN_SCALE_FACTOR) == "1";
}

void setQtAutoScreenScaleFactorEnabled(bool enable)
{
	QByteArray value;
	value.setNum(enable ? 1 : 0);

	qputenv(QT_AUTO_SCREEN_SCALE_FACTOR, value);
}

const char QT_SCALE_FACTOR[] = "QT_SCALE_FACTOR";

std::optional<double> getQtScaleFactor()
{
	bool ok;
	double scaleFactor = qgetenv(QT_SCALE_FACTOR).toDouble(&ok);
	if (ok)
		return scaleFactor;
	else
		return {};
}

void setQtScaleFactor(double factor)
{
	QByteArray value;
	value.setNum(factor);

	qputenv(QT_SCALE_FACTOR, value);
}
}	 // namespace utility
