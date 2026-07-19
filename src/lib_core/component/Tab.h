#ifndef TAB_H
#define TAB_H

#include <vector>

#include "ComponentManager.h"
#include "MessageFocusView.h"
#include "MessageListener.h"
#include "MessageRefreshUI.h"
#include "View.h"
#include "ViewLayout.h"

class Tab
	: public ViewLayout
	, public MessageListener<MessageFocusView>
	, public MessageListener<MessageRefreshUI>
{
public:
	Tab(TabId tabId,
		const ViewFactory* viewFactory,
		StorageAccess* storageAccess,
		ScreenSearchSender* screenSearchSender);
	~Tab() override;

	TabId getSchedulerId() const override;

	void setParentLayout(ViewLayout* parentLayout);

	// ViewLayout implementation
	void addView(View* view) override;
	void removeView(View* view) override;

	void showView(View* view) override;
	void hideView(View* view) override;

	void setViewEnabled(View* view, bool enabled) override;

private:
	void handleMessage(MessageFocusView* message) override;
	void handleMessage(MessageRefreshUI* message) override;

	const TabId m_tabId;

	ComponentManager m_componentManager;
	std::vector<View*> m_views;

	ViewLayout* m_parentLayout = nullptr;
	ScreenSearchSender* m_screenSearchSender;
};

#endif	  // TAB_H
