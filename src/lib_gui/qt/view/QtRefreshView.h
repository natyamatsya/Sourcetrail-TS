#ifndef QT_REFRESH_VIEW_H
#define QT_REFRESH_VIEW_H

#include "MessageIndexOutdated.h"
#include "MessageListener.h"
#include "RefreshView.h"

class QFrame;
class QLabel;
class QPushButton;

class QtRefreshView
	: public RefreshView
	, public MessageListener<MessageIndexOutdated>
{
public:
	QtRefreshView(ViewLayout* viewLayout);
	~QtRefreshView() override = default;

	// View implementation
	void createWidgetWrapper() override;
	void refreshView() override;

private:
	// A dot on the button while indexed files changed on disk.
	void handleMessage(MessageIndexOutdated* message) override;

	QFrame* m_widget;
	QPushButton* m_refreshButton;
	QLabel* m_outdatedDot;
};

#endif	  // QT_REFRESH_VIEW_H
