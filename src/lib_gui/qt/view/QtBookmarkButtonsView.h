#ifndef QT_BOOKMARK_BUTTONS_VIEW_H
#define QT_BOOKMARK_BUTTONS_VIEW_H

#include <QObject>

#include "BookmarkButtonsView.h"


class QFrame;
class QtSearchBarButton;

class QtBookmarkButtonsView
	: public QObject
	, public BookmarkButtonsView
{
	Q_OBJECT

public:
	QtBookmarkButtonsView(ViewLayout* viewLayout);
	~QtBookmarkButtonsView() override = default;

	// View implementation
	void createWidgetWrapper() override;
	void refreshView() override;

	// BookmarkView implementation
	void setCreateButtonState(const MessageBookmarkButtonState::ButtonState& state) override;

private slots:
	void createBookmarkClicked();
	static void showBookmarksClicked();

private:

	QFrame* m_widget;

	QtSearchBarButton* m_createBookmarkButton;
	QtSearchBarButton* m_showBookmarksButton;

	MessageBookmarkButtonState::ButtonState m_createButtonState = MessageBookmarkButtonState::ButtonState::CANNOT_CREATE;
};

#endif	  // QT_BOOKMARK_BUTTONS_VIEW_H
