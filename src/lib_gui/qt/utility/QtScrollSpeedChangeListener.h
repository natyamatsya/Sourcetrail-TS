#ifndef QT_SCROLL_SPEED_CHANGE_LISTENER
#define QT_SCROLL_SPEED_CHANGE_LISTENER

#include "MessageListener.h"
#include "MessageScrollSpeedChange.h"

class QScrollBar;

class QtScrollSpeedChangeListener: public MessageListener<MessageScrollSpeedChange>
{
public:
	QtScrollSpeedChangeListener();

	void setScrollBar(QScrollBar* scrollbar);

private:
	void handleMessage(MessageScrollSpeedChange* message) override;

	void doChangeScrollSpeed(float scrollSpeed);

	QScrollBar* m_scrollBar = nullptr;
	int m_singleStep = 1;
};

#endif	  // QT_SCROLL_SPEED_CHANGE_LISTENER
