#ifndef QT_SCREEN_SEARCH_VIEW_H
#define QT_SCREEN_SEARCH_VIEW_H

#include <QObject>

#include "ControllerProxy.h"
#include "ScreenSearchController.h"
#include "ScreenSearchView.h"

class QtScreenSearchBox;
class QToolBar;

class QtScreenSearchView
	: public QObject
	, public ScreenSearchView
{
	Q_OBJECT

public:
	QtScreenSearchView(ViewLayout* viewLayout);
	~QtScreenSearchView() override = default;

	// View implementation
	void createWidgetWrapper() override;
	void refreshView() override;

	// ScreenSearchView implementation
	void setMatchCount(size_t matchCount) override;
	void setMatchIndex(size_t matchIndex) override;

	void addResponder(const std::string& name) override;

public slots:
	void show();
	void hide();

private:
	ControllerProxy<ScreenSearchController> m_controllerProxy;

	QtScreenSearchBox* m_widget;
	QToolBar* m_bar;
};

#endif	  // QT_SCREEN_SEARCH_VIEW_H
