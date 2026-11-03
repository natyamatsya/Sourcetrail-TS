#ifndef QT_CODE_FILE_LIST
#define QT_CODE_FILE_LIST

#include <cstdint>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QFrame>
#include <QScrollArea>

#include "CodeSnippetParams.h"
#include "QtCodeNavigable.h"
#include "QtScrollSpeedChangeListener.h"

class QtCodeFile;
class QtCodeFileTitleBar;
class QtCodeNavigator;
class QtCodeSnippet;

class QtCodeFileListScrollArea: public QScrollArea
{
protected:
	void keyPressEvent(QKeyEvent* event) override;
};

class QtCodeFileList
	: public QFrame
	, public QtCodeNavigable
{
	Q_OBJECT

public:
	QtCodeFileList(QtCodeNavigator* navigator);
	~QtCodeFileList() override = default;

	void clear();
	void clearSnippetTitleAndScrollBar();

	QtCodeFile* getFile(const FilePath& filePath);

	void addFile(const CodeFileParams& params);

	// QtCodeNavigable implementation
	QScrollArea* getScrollArea() override;
	// A whole file alone in the list scrolls its lines itself.
	QAbstractScrollArea* getPositionScrollArea() override;

	void updateSourceLocations(const CodeSnippetParams& params) override;
	void updateFiles() override;

	void scrollTo(
		const FilePath& filePath,
		size_t lineNumber,
		Id locationId,
		Id scopeLocationId,
		bool animated,
		CodeScrollParams::Target target,
		bool focusTarget) override;

	void onWindowFocus() override;

	void findScreenMatches(
		const std::string& query, std::vector<std::pair<QtCodeArea*, Id>>* screenMatches) override;

	void setFocus(Id locationId) override;
	void setFocusOnTop() override;
	void moveFocus(const CodeFocusHandler::Focus& focus, CodeFocusHandler::Direction direction) override;

	void copySelection() override;

	void maximizeFirstFile();

	std::pair<QtCodeFile*, Id> getFirstFileWithActiveLocationId() const;
	std::pair<QtCodeSnippet*, Id> getFirstSnippetAndActiveLocationId() const;

protected:
	void resizeEvent(QResizeEvent* event) override;

private slots:
	void updateSnippetTitleAndScrollBarSlot();
	void updateSnippetTitleAndScrollBar(int value = 0);

	void scrollLastSnippet(int value);
	void scrollLastSnippetScrollBar(int value);

private:
	void updateFirstSnippetTitleBar(QtCodeFile* file, int fileTitleBarOffset = 0);
	void updateLastSnippetScrollBar(QScrollBar* mirroredScrollBar);
	// A whole file is at most as high as the list shows: it scrolls its lines itself then,
	// and only what shows is laid out (QPlainTextEdit's way), not all of a big file.
	void updateWholeFileHeights();

	QtCodeNavigator* m_navigator;
	QtCodeFileListScrollArea* m_scrollArea;
	QFrame* m_filesArea;

	std::vector<QtCodeFile*> m_files;

	// getFile's lookups, by the path's text and by the file on disk. They stand in for comparing
	// the path with every listed file's (FilePath's == stats both files), which made listing many
	// files quadratic in stat calls.
	using FileIdentity = std::pair<std::uint64_t, std::uint64_t>;
	std::unordered_map<std::filesystem::path::string_type, QtCodeFile*> m_filesByPath;
	std::map<FileIdentity, QtCodeFile*> m_filesByIdentity;

	QtCodeFileTitleBar* m_firstSnippetTitleBar;
	const QtCodeFile* m_firstSnippetFile = nullptr;
	const QtCodeFileTitleBar* m_mirroredTitleBar = nullptr;

	QScrollBar* m_lastSnippetScrollBar;
	QScrollBar* m_mirroredSnippetScrollBar = nullptr;

	QtScrollSpeedChangeListener m_scrollSpeedChangeListener;
};

#endif	  // QT_CODE_FILE_LIST
