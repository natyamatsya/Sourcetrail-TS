#include "QtIndexingStartDialog.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>

#include "QtHelpButton.h"

QtIndexingStartDialog::QtIndexingStartDialog(
	const std::vector<RefreshMode>& enabledModes,
	const RefreshMode initialMode,
	const QString& reason,
	QWidget* parent)
	: QtIndexingDialog(true, parent)
{
	setSizeGripStyle(false);

	QtIndexingDialog::createTitleLabel(QStringLiteral("Start Indexing"), m_layout);
	m_layout->addSpacing(5);

	// Why the choice opened (the project's settings changed), in place of a question
	// asked before it.
	if (!reason.isEmpty())
	{
		QLabel* reasonLabel = QtIndexingDialog::createMessageLabel(m_layout);
		reasonLabel->setObjectName(QStringLiteral("reason"));
		reasonLabel->setAccessibleName(QStringLiteral("Why indexing is suggested"));
		reasonLabel->setWordWrap(true);
		reasonLabel->setText(reason);
		m_layout->addSpacing(10);
	}

	m_clearLabel = QtIndexingDialog::createMessageLabel(m_layout);
	m_indexLabel = QtIndexingDialog::createMessageLabel(m_layout);

	m_clearLabel->setVisible(false);
	m_indexLabel->setVisible(false);

	m_layout->addStretch();

	QHBoxLayout* subLayout = new QHBoxLayout();
	subLayout->addStretch();

	QVBoxLayout* modeLayout = new QVBoxLayout();
	modeLayout->setSpacing(7);

	QHBoxLayout* modeTitleLayout = new QHBoxLayout();
	modeTitleLayout->setSpacing(7);

	QLabel* modeLabel = QtIndexingDialog::createMessageLabel(modeTitleLayout);
	modeLabel->setText(QStringLiteral("Mode:"));
	modeLabel->setAlignment(Qt::AlignLeft);

	QtHelpButton* helpButton = new QtHelpButton(QtHelpButtonInfo(
		QStringLiteral("Indexing Modes"),
		QString("<b>Updated files:</b> Reindexes all files that were modified since the last "
				"indexing, all new files and all files depending "
				"on those.<br /><br />"
				"<b>Incomplete & updated files:</b> Reindexes all files that had errors during "
				"last indexing, all updated files and all files "
				"depending on those.<br /><br />"
				"<b>All files:</b> Deletes the previous index and reindexes all files from "
				"scratch.<br /><br />")));
	helpButton->setColor(Qt::white);
	modeTitleLayout->addWidget(helpButton);

	modeTitleLayout->addStretch();

	modeLayout->addLayout(modeTitleLayout);
	modeLayout->addSpacing(5);

	m_refreshModeButtons.emplace(
		RefreshMode::UPDATED_FILES, new QRadioButton(QStringLiteral("Updated files")));
	m_refreshModeButtons.emplace(
		RefreshMode::UPDATED_AND_INCOMPLETE_FILES,
		new QRadioButton(QStringLiteral("Incomplete && updated files")));
	m_refreshModeButtons.emplace(RefreshMode::ALL_FILES, new QRadioButton(QStringLiteral("All files")));

	std::function<void(bool)> func = [=, this](bool checked) {
		if (!checked)
		{
			return;
		}

		for (auto p: m_refreshModeButtons)
		{
			if (p.second->isChecked())
			{
				emit setMode(p.first);
				return;
			}
		}
	};

	for (auto p: m_refreshModeButtons)
	{
		QRadioButton* button = p.second;
		button->setObjectName(QStringLiteral("option"));
		button->setEnabled(false);
		if (p.first == initialMode)
		{
			button->setChecked(true);
		}
		modeLayout->addWidget(button);
		connect(button, &QRadioButton::toggled, func);
	}

	for (RefreshMode mode: enabledModes)
	{
		m_refreshModeButtons[mode]->setEnabled(true);
	}

	subLayout->addLayout(modeLayout);
	m_layout->addLayout(subLayout);

	m_layout->addSpacing(20);

	{
		QHBoxLayout* buttons = new QHBoxLayout();
		QPushButton* cancelButton = new QPushButton(QStringLiteral("Cancel"));
		cancelButton->setObjectName(QStringLiteral("windowButton"));
		connect(cancelButton, &QPushButton::clicked, this, &QtIndexingStartDialog::onCancelPressed);
		buttons->addWidget(cancelButton);

		buttons->addStretch();

		m_startButton = new QPushButton(QStringLiteral("Start"));
		m_startButton->setObjectName(QStringLiteral("windowButton"));
		m_startButton->setDefault(true);
		connect(m_startButton, &QPushButton::clicked, this, &QtIndexingStartDialog::onStartPressed);
		buttons->addWidget(m_startButton);
		m_layout->addLayout(buttons);
	}

	setupDone();
}

QSize QtIndexingStartDialog::sizeHint() const
{
	return QSize(350, 310);
}

void QtIndexingStartDialog::updateRefreshInfo(const RefreshInfo& info)
{
	QRadioButton* button = m_refreshModeButtons.find(info.mode)->second;
	if (!button->isChecked())
	{
		button->setChecked(true);
	}

	size_t clearCount = info.filesToClear.size();
	size_t indexCount = info.filesToIndex.size();

	m_clearLabel->setText("Files to clear: " + QString::number(clearCount));
	m_indexLabel->setText("Source files to index: " + QString::number(indexCount));

	m_clearLabel->setVisible(clearCount && info.mode != RefreshMode::ALL_FILES);
	m_indexLabel->setVisible(true);

	// An update with nothing to index or clear would do nothing.
	const bool nothingToDo = info.mode != RefreshMode::ALL_FILES && clearCount == 0 && indexCount == 0;
	if (nothingToDo)
	{
		m_indexLabel->setText(QStringLiteral("All files are up to date."));
	}
	m_startButton->setEnabled(!nothingToDo);
}

void QtIndexingStartDialog::resizeEvent(QResizeEvent* event)
{
	QtIndexingDialog::resizeEvent(event);
}

void QtIndexingStartDialog::closeEvent(QCloseEvent*  /*event*/)
{
	emit QtIndexingDialog::canceled();
}

void QtIndexingStartDialog::keyPressEvent(QKeyEvent* event)
{
	switch (event->key())
	{
	case Qt::Key_Escape:
		onCancelPressed();
		break;
	case Qt::Key_Return:
		if (m_startButton->isEnabled())
		{
			onStartPressed();
		}
		break;
	}

	QWidget::keyPressEvent(event);
}

void QtIndexingStartDialog::onStartPressed()
{
	for (auto p: m_refreshModeButtons)
	{
		if (p.second->isChecked())
		{
			emit startIndexing(p.first);
			return;
		}
	}

	emit finished();
}

void QtIndexingStartDialog::onCancelPressed()
{
	emit canceled();
}
