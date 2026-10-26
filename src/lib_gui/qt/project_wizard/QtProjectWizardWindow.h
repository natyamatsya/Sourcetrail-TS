#ifndef QT_PROJECT_WIZARD_WINDOW_H
#define QT_PROJECT_WIZARD_WINDOW_H

#include <string>
#include <vector>

#include "QtWindow.h"

class QtProjectWizardContent;

class QtProjectWizardWindow: public QtWindow
{
	Q_OBJECT

public:
	QtProjectWizardWindow(QWidget* parent, bool showSeparator = true);
	QSize sizeHint() const override;

	QtProjectWizardContent* content() const;
	void setContent(QtProjectWizardContent* content);

	void setPreferredSize(QSize size);

	void saveContent();
	void loadContent();
	void refreshContent();

	// What keeps the wizard from going on, told the person as a question
	// (QuestionStateMachine), which an agent answers as well; returns once answered.
	void tell(const QString& text);
	// Asks the person: the index of the option chosen, -1 for none.
	int ask(const QString& text, const std::vector<std::string>& options);
	// What was told or asked last: why an agent's request did not go through.
	QString lastMessage() const;
	void clearLastMessage();

	// A page's content changed on its own (work it ran in the background finished):
	// what an agent sees of the page follows.
	virtual void contentChanged();

	static const int FRONT_COL = 0;
	static const int HELP_COL = 1;
	static const int LINE_COL = 2;
	static const int BACK_COL = 3;

protected:
	// QtWindow implementation
	void populateWindow(QWidget* widget) override;
	void windowReady() override;

	void handleNext() override;
	void handlePrevious() override;

private:
	QtProjectWizardContent* m_content = nullptr;
	QSize m_preferredSize;
	bool m_showSeparator;
	QString m_lastMessage;
};

#endif	  // QT_PROJECT_WIZARD_WINDOW_H
