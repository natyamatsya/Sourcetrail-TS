#ifndef QT_BOOKMARK_CREATOR_H
#define QT_BOOKMARK_CREATOR_H

#include <QWidget>

#include <functional>

#include "BookmarkController.h"
#include "BookmarkStateMachine.h"
#include "ControllerProxy.h"
#include "QtBookmarkWindow.h"

class BookmarkCategory;
class QComboBox;
class QLineEdit;
class QTextEdit;

class QtBookmarkCreator: public QtBookmarkWindow
{
	Q_OBJECT

public:
	QtBookmarkCreator(ControllerProxy<BookmarkController>* controllerProxy, QWidget* parent = nullptr, BookmarkId bookmarkId = BookmarkId::NONE);
	
	void setDisplayName(const std::string& name);
	void setComment(const std::string& comment);
	
	void setBookmarkCategories(const std::vector<BookmarkCategory>& categories);
	void setCurrentBookmarkCategory(const BookmarkCategory& category);
	
	// What saving does, and which of the bookmark machine's editors this one is: its
	// buttons and fields go to the machine.
	void setCommit(std::function<void(const BookmarkFields&)> commit);
	void setEditorId(std::uint64_t editorId);
	// Fills the fields (an agent set them).
	void showFields(const BookmarkFields& fields);

protected:
	void handleNext() override;
	void handleClose() override;
	
private slots:
	void onNameChanged(const QString& text);
	void onFieldsChanged();

private:
	BookmarkFields fields() const;

	const BookmarkId m_editBookmarkId;

	QLineEdit* m_displayName;
	QTextEdit* m_commentBox;
	QComboBox* m_categoryBox;

	std::function<void(const BookmarkFields&)> m_commit;
	std::uint64_t m_editorId = 0;
};

#endif	  // QT_BOOKMARK_CREATOR_H
