#include "QtProjectWizardContentPathMakefile.h"

#include <format>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>

#include "ProjectWizardStateMachine.h"
#include "QtProjectWizardContentPathsIndexedHeaders.h"
#include "SourceGroupCxxMake.h"
#include "SourceGroupSettingsCxxMake.h"
#include "utility.h"
#include "utilityFile.h"
#include "utilitySourceGroupCxx.h"

QtProjectWizardContentPathMakefile::QtProjectWizardContentPathMakefile(
	std::shared_ptr<SourceGroupSettingsCxxMake> settings, QtProjectWizardWindow* window)
	: QtProjectWizardContentPath(window), m_settings(settings), m_filePaths([&]() {
		if (!m_dryRun || !*m_dryRun)
		{
			return std::vector<FilePath>{};
		}
		return utility::getAsRelativeIfShorter(
			utility::toVector(SourceGroupCxxCdb(m_settings).getAllSourceFilePaths()),
			m_settings->getProjectDirectoryPath());
	})
{
	setTitleString(QStringLiteral("Makefile Directory"));
	setHelpString(
		"The directory make runs in: the one with the project's Makefile (GNUmakefile, makefile or "
		"Makefile).<br />"
		"<br />"
		"Sourcetrail reads the compile commands from what make prints for a dry run "
		"(<tt>make -n -B</tt>), which builds nothing, and writes them to a compilation database of "
		"its own. It does so again on every refresh, so the project stays up to date with the "
		"Makefile.<br />"
		"<br />"
		"You can make use of environment variables with ${ENV_VAR}.");
	setIsRequired(true);
}

void QtProjectWizardContentPathMakefile::populate(QGridLayout* layout, int& row)
{
	QtProjectWizardContentPath::populate(layout, row);
	m_picker->setPickDirectory(true);
	connect(
		m_picker,
		&QtLocationPicker::textChanged,
		this,
		&QtProjectWizardContentPathMakefile::onDirectoryChanged);

	QLabel* description = new QLabel(
		"Sourcetrail will use the compiler flags of every file make would compile, and stay "
		"up-to-date with the Makefile on refresh.",
		this);
	description->setObjectName(QStringLiteral("description"));
	description->setWordWrap(true);
	layout->addWidget(description, row, QtProjectWizardWindow::BACK_COL);
	row++;

	{
		QLabel* label = createFormSubLabel(QStringLiteral("Goal and Variables"));
		layout->addWidget(label, row, QtProjectWizardWindow::FRONT_COL, Qt::AlignRight);

		m_argumentsCombo = new QComboBox(this);
		m_argumentsCombo->setObjectName(QStringLiteral("makeArguments"));
		m_argumentsCombo->setAccessibleName(QStringLiteral("Goal and Variables"));
		m_argumentsCombo->setEditable(true);
		m_argumentsCombo->setInsertPolicy(QComboBox::NoInsert);
		m_argumentsCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_argumentsCombo->lineEdit()->setPlaceholderText(
			QStringLiteral("The default goal; or e.g. all, osx-arm64-debug, config=debug"));
		m_argumentsCombo->lineEdit()->setAttribute(Qt::WA_MacShowFocusRect, false);
		layout->addWidget(m_argumentsCombo, row, QtProjectWizardWindow::BACK_COL);
		connect(
			m_argumentsCombo->lineEdit(),
			&QLineEdit::editingFinished,
			this,
			&QtProjectWizardContentPathMakefile::onArgumentsEdited);
		connect(m_argumentsCombo, &QComboBox::activated, this, [this](int) { onArgumentsEdited(); });

		addHelpButton(
			QStringLiteral("Goal and Variables"),
			QStringLiteral(
				"<p>What make is given after its options, as you would type it:</p>"
				"<pre>make -n -B -w -k &lt;goal and variables&gt;</pre>"
				"<p>The goal that builds the project (<tt>all</tt>, or one per platform and "
				"configuration such as <tt>osx-arm64-debug</tt>), and variables that choose its "
				"flags (<tt>config=debug</tt>). The list holds the makefile's goals, the likeliest "
				"first; leave it empty for its default goal.</p>"
				"<p>A dry run runs no recipe but the makes it starts, so makefiles a generator "
				"would write first (premake, GENie) have to be there: run the generator once, or "
				"pick the directory it wrote them to.</p>"),
			layout,
			row);
		row++;
	}

	QLabel* title = createFormSubLabel(QStringLiteral("Source Files to Index"));
	layout->addWidget(title, row, QtProjectWizardWindow::FRONT_COL, Qt::AlignTop);
	layout->setRowStretch(row, 0);

	m_fileCountLabel = new QLabel(QLatin1String(""));
	m_fileCountLabel->setObjectName(QStringLiteral("makeDryRun"));
	m_fileCountLabel->setAccessibleName(QStringLiteral("Dry Run"));
	m_fileCountLabel->setWordWrap(true);
	m_fileCountLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_fileCountLabel, row, QtProjectWizardWindow::BACK_COL, Qt::AlignTop);
	row++;

	addFilesButton(QStringLiteral("show source files"), layout, row);
	row++;
}

void QtProjectWizardContentPathMakefile::load()
{
	m_picker->blockSignals(true);
	m_picker->setText(QString::fromStdString(m_settings->getMakefileDirectory().str()));
	m_picker->blockSignals(false);
	if (m_argumentsCombo)
	{
		m_argumentsCombo->setEditText(QString::fromStdString(m_settings->getMakeArguments()));
	}
	listTargets();
	runDryRun();
	refresh();
}

void QtProjectWizardContentPathMakefile::save()
{
	m_settings->setMakefileDirectory(FilePath(m_picker->getText().toStdString()));
	if (m_argumentsCombo)
	{
		m_settings->setMakeArguments(m_argumentsCombo->currentText().trimmed().toStdString());
	}
	m_settings->useGeneratedCompilationDatabase();
}

void QtProjectWizardContentPathMakefile::refresh()
{
	m_filePaths.clear();
	if (!m_fileCountLabel)
	{
		return;
	}

	if (!m_dryRun)
	{
		m_fileCountLabel->setText(QLatin1String(""));
		return;
	}
	if (!*m_dryRun)
	{
		m_fileCountLabel->setText(
			"<b>0</b> source files: " +
			QString::fromStdString(make::describe(m_dryRun->error())).toHtmlEscaped().replace('\n', "<br />"));
		return;
	}
	const std::size_t count = getFilePaths().size();
	m_fileCountLabel->setText(
		"<b>" + QString::number(count) + "</b>" +
		(count == 1 ? " source file is compiled by " : " source files are compiled by ") + "<tt>" +
		QString::fromStdString((*m_dryRun)->commandLine).toHtmlEscaped() + "</tt>.");
}

bool QtProjectWizardContentPathMakefile::check()
{
	if (!QtProjectWizardContentPath::check())
	{
		return false;
	}
	save();
	runDryRun();
	refresh();
	if (!*m_dryRun)
	{
		m_window->tell(QString::fromStdString(make::describe(m_dryRun->error())));
		return false;
	}
	return true;
}

void QtProjectWizardContentPathMakefile::describe(ProjectWizardView& view) const
{
	std::vector<ProjectWizardView::Choice>& goals = view.choices["make_arguments"];
	for (const std::string& goal: m_goals)
	{
		goals.push_back({.value = goal, .label = {}});
	}
	if (m_dryRun)
	{
		view.notes["make_arguments"] = *m_dryRun
			? std::format(
				  "{} source files are compiled by {}", getFilePaths().size(), (*m_dryRun)->commandLine)
			: make::describe(m_dryRun->error());
	}
}

std::vector<FilePath> QtProjectWizardContentPathMakefile::getFilePaths() const
{
	return m_filePaths.getValue();
}

QString QtProjectWizardContentPathMakefile::getFileNamesTitle() const
{
	return QStringLiteral("Source Files");
}

QString QtProjectWizardContentPathMakefile::getFileNamesDescription(std::size_t count) const
{
	return QString::number(count) +
		(count == 1 ? QStringLiteral(" source file will be indexed.") : QStringLiteral(" source files will be indexed."));
}

void QtProjectWizardContentPathMakefile::onDirectoryChanged(const QString& text)
{
	const FilePath directory = utility::getExpandedAndAbsolutePath(
		FilePath(text.toStdString()), m_settings->getProjectDirectoryPath());
	if (directory.empty() || !directory.exists() || make::findMakefile(directory.str()).empty() ||
		directory == m_settings->getMakefileDirectoryExpandedAndAbsolute())
	{
		return;
	}
	m_window->saveContent();
	// Another makefile: its own goals.
	if (m_argumentsCombo)
	{
		m_argumentsCombo->setEditText({});
	}
	save();
	listTargets();
	runDryRun();
	deriveIndexedHeaderPaths();
	m_window->loadContent();
}

void QtProjectWizardContentPathMakefile::onArgumentsEdited()
{
	if (QString::fromStdString(m_settings->getMakeArguments()) == m_argumentsCombo->currentText().trimmed())
	{
		return;
	}
	m_window->saveContent();
	runDryRun();
	deriveIndexedHeaderPaths();
	m_window->loadContent();
}

std::shared_ptr<SourceGroupSettings> QtProjectWizardContentPathMakefile::getSourceGroupSettings()
{
	return m_settings;
}

void QtProjectWizardContentPathMakefile::listTargets()
{
	const QString directory = QString::fromStdString(m_settings->getMakefileDirectoryExpandedAndAbsolute().str());
	if (!m_argumentsCombo || directory == m_listedDirectory)
	{
		return;
	}
	m_listedDirectory = directory;

	const make::Invocation invocation{.directory = directory.toStdString(), .program = m_settings->getMakeProgram()};
	const make::Targets targets = make::listTargets(invocation);
	// The likeliest first, then the others by name.
	std::vector<std::string> goals = make::rankTargets(targets);
	std::vector<std::string> others;
	for (const std::string& target: targets.targets)
	{
		if (!std::ranges::contains(goals, target))
		{
			others.push_back(target);
		}
	}
	std::ranges::sort(others);
	goals.insert(goals.end(), others.begin(), others.end());

	m_goals = goals;
	const QString arguments = m_argumentsCombo->currentText();
	m_argumentsCombo->blockSignals(true);
	m_argumentsCombo->clear();
	for (const std::string& goal: goals)
	{
		m_argumentsCombo->addItem(QString::fromStdString(goal));
	}
	m_argumentsCombo->setEditText(arguments);
	m_argumentsCombo->blockSignals(false);

	// No goal set yet: the first that compiles anything, or the default goal.
	if (m_settings->getMakeArguments().empty())
	{
		if (const std::optional<make::Suggestion> suggestion = make::suggestTarget(invocation))
		{
			m_settings->setMakeArguments(suggestion->target);
			m_argumentsCombo->setEditText(QString::fromStdString(suggestion->target));
		}
	}
}

void QtProjectWizardContentPathMakefile::runDryRun()
{
	if (m_settings->getMakefileDirectory().empty())
	{
		m_dryRun.reset();
		m_dryRunOf.clear();
		return;
	}
	const std::string of = m_settings->getMakefileDirectoryExpandedAndAbsolute().str() + '\n' +
		m_settings->getMakeArguments() + '\n' + m_settings->getMakeProgram() + '\n' +
		m_settings->getGeneratedCompilationDatabasePath().str();
	if (m_dryRun && of == m_dryRunOf && m_settings->getGeneratedCompilationDatabasePath().exists())
	{
		return;
	}
	m_dryRun = SourceGroupCxxMake::writeCompilationDatabase(*m_settings);
	m_dryRunOf = of;
	m_filePaths.clear();
}

void QtProjectWizardContentPathMakefile::deriveIndexedHeaderPaths()
{
	if (!m_dryRun || !*m_dryRun)
	{
		return;
	}
	const FilePath projectPath = m_settings->getProjectDirectoryPath();
	std::set<FilePath> indexedHeaderPaths;
	for (const FilePath& path: QtProjectWizardContentPathsIndexedHeaders::getIndexedPathsDerivedFromCDB(m_settings))
	{
		if (projectPath.contains(path))
		{
			indexedHeaderPaths.insert(path.getRelativeTo(projectPath));
		}
	}
	m_settings->setIndexedHeaderPaths(utility::toVector(indexedHeaderPaths));
}
