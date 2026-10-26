#ifndef QT_ICON_BUTTON_H
#define QT_ICON_BUTTON_H

#include <QPushButton>

#include "FilePath.h"

class QtIconButton: public QPushButton
{
	Q_OBJECT
public:
	QtIconButton(const FilePath& iconPath, const FilePath& hoveredIconPath, QWidget* parent = nullptr);
	~QtIconButton() override = default;

	void setColor(QColor color);

protected:
	bool event(QEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	void setIconFromPath(const FilePath& path);

	const FilePath m_iconPath;
	const FilePath m_hoveredIconPath;

	QColor m_color;
	QString m_toolTipName;	// the accessible name the tooltip gave
};

#endif	  // QT_ICON_BUTTON_H
