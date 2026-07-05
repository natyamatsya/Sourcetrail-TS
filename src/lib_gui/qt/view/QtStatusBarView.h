#ifndef QT_STATUS_BAR_VIEW_H
#define QT_STATUS_BAR_VIEW_H

#include <string>

#include "ErrorCountInfo.h"
#include "QtStatusBar.h"
#include "StatusBarView.h"

class QtStatusBarView: public StatusBarView
{
public:
	QtStatusBarView(ViewLayout* viewLayout);
	~QtStatusBarView() override = default;

	// View implementation
	void createWidgetWrapper() override;
	void refreshView() override;

	// StatusBar view implementation
	void showMessage(const std::string& message, bool isError, bool showLoader) override;
	void setErrorCount(ErrorCountInfo errorCount) override;

	void showIdeStatus(const std::string& message) override;

	void showIndexingProgress(size_t progressPercent) override;
	void hideIndexingProgress() override;

	void showTextEncoding(const std::string &encoding) override;
	
private:
	QtStatusBar* m_statusBar;
};

#endif	  // !QT_STATUS_BAR_VIEW_H
