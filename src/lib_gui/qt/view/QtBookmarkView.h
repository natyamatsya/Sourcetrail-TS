#ifndef QT_BOOKMARK_VIEW_H
#define QT_BOOKMARK_VIEW_H

#include <QPointer>

#include "BookmarkController.h"
#include "BookmarkStateMachine.h"
#include "BookmarkView.h"
#include "ControllerProxy.h"

class QFrame;
class QtBookmarkBrowser;
class QtBookmarkCreator;
class QtMainWindow;

class QtBookmarkView: public BookmarkView
{
public:
	QtBookmarkView(ViewLayout* viewLayout);
	~QtBookmarkView() override;

	// View implementation
	void createWidgetWrapper() override;
	void refreshView() override;

	// BookmarkView implementation
	void displayBookmarkCreator(const std::vector<std::string>& names, const std::vector<BookmarkCategory>& categories, Id nodeId) override;
	void displayBookmarkCreator(std::shared_ptr<Bookmark> bookmark, const std::vector<BookmarkCategory>& categories) override;

	void displayBookmarkBrowser(const std::vector<std::shared_ptr<Bookmark>>& bookmarks) override;
	
	void undisplayBookmarkBrowser() override;
	
	bool bookmarkBrowserIsVisible() const override;

	// What the bookmark machine shows of an agent's doing in the editor
	// (PresentsBookmarkEditor).
	void presentBookmarkFields(const BookmarkFields& fields);
	void dismissBookmarkEditor();

private:
	// Shows `creator` in place of the one open, and tells the bookmark machine.
	void openCreator(
		QtBookmarkCreator* creator, BookmarkStateMachine::Editor editor, std::function<void(const BookmarkFields&)> commit);

	ControllerProxy<BookmarkController> m_controllerProxy;

	QtBookmarkBrowser* m_bookmarkBrowser = nullptr;
	QPointer<QtBookmarkCreator> m_creator;	// the editor open, if one is
};

#endif	  // QT_BOOKMARK_VIEW_H
