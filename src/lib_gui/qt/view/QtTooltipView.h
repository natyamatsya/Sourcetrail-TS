#ifndef QT_TOOLTIP_VIEW
#define QT_TOOLTIP_VIEW

#include "TooltipView.h"

class QTimer;
class QtTooltip;

class QtTooltipView: public TooltipView
{
public:
	QtTooltipView(ViewLayout* viewLayout);
	~QtTooltipView() override = default;

	// View implementation
	void createWidgetWrapper() override;
	void refreshView() override;

	// TooltipView implementation
	void showTooltip(const TooltipInfo& info, const View* parent) override;
	void hideTooltip(bool force) override;

	bool tooltipVisible() const override;

private:

	QtTooltip* m_widget;
};

#endif	  // QT_TOOLTIP_VIEW
