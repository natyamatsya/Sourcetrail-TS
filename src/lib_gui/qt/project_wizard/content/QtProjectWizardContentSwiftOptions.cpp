#include "QtProjectWizardContentSwiftOptions.h"

#include <QComboBox>
#include <QLineEdit>

#include "QtLocationPicker.h"
#include "SourceGroupSettingsWithSwiftOptions.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utilityString.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.utility;
#endif

QtProjectWizardContentSwiftOptions::QtProjectWizardContentSwiftOptions(
	std::shared_ptr<SourceGroupSettingsWithSwiftOptions> settings, QtProjectWizardWindow* window)
	: QtProjectWizardContent(window), m_settings(settings)
{
}

void QtProjectWizardContentSwiftOptions::populate(QGridLayout* layout, int& row)
{
	m_buildArgs = new QLineEdit();
	m_buildArgs->setObjectName(QStringLiteral("swift_build_args"));
	m_buildArgs->setAttribute(Qt::WA_MacShowFocusRect, false);
	m_buildArgs->setPlaceholderText(QStringLiteral("--configuration release"));

	layout->addWidget(
		createFormLabel(QStringLiteral("Swift Build Args")),
		row,
		QtProjectWizardWindow::FRONT_COL,
		Qt::AlignRight);
	layout->addWidget(m_buildArgs, row, QtProjectWizardWindow::BACK_COL);
	addHelpButton(
		QStringLiteral("Swift Build Args"),
		"<p>Extra arguments appended to the <b>swift build</b> invocation that produces the index "
		"store (space or comma separated), e.g. <b>--configuration release</b> or "
		"<b>-Xswiftc -DFOO</b>. Conditionally compiled code that is not enabled is invisible to "
		"the index.</p>",
		layout,
		row);
	row++;

	m_toolchainPath = new QtLocationPicker(this);
	m_toolchainPath->setObjectName(QStringLiteral("swift_toolchain_path"));
	m_toolchainPath->setPickDirectory(true);
	m_toolchainPath->setPlaceholderText(QStringLiteral("default toolchain (resolved via xcrun)"));

	layout->addWidget(
		createFormLabel(QStringLiteral("Swift Toolchain")),
		row,
		QtProjectWizardWindow::FRONT_COL,
		Qt::AlignRight);
	layout->addWidget(m_toolchainPath, row, QtProjectWizardWindow::BACK_COL);
	addHelpButton(
		QStringLiteral("Swift Toolchain"),
		"<p>Toolchain root to build and index with &mdash; the directory containing "
		"<b>usr/bin/swift</b> and <b>usr/lib/libIndexStore.dylib</b> (e.g. an "
		"<b>.xctoolchain</b>).</p>"
		"<p>Leave empty to use the default toolchain resolved via <b>xcrun</b>.</p>",
		layout,
		row);
	row++;

	m_indexStorePath = new QtLocationPicker(this);
	m_indexStorePath->setObjectName(QStringLiteral("swift_index_store_path"));
	m_indexStorePath->setPickDirectory(true);
	m_indexStorePath->setPlaceholderText(QStringLiteral("built from source"));

	layout->addWidget(
		createFormLabel(QStringLiteral("Index Store")),
		row,
		QtProjectWizardWindow::FRONT_COL,
		Qt::AlignRight);
	layout->addWidget(m_indexStorePath, row, QtProjectWizardWindow::BACK_COL);
	addHelpButton(
		QStringLiteral("Index Store"),
		"<p>An existing compiler index store to read. When set, Sourcetrail <b>skips</b> "
		"<b>swift build</b> and indexes the store directly &mdash; useful for a read-only or "
		"prebuilt checkout whose <b>.build</b> already holds index units.</p>"
		"<p>Leave empty to build the package and produce the store as part of indexing.</p>",
		layout,
		row);
	row++;

	m_specializationScope = new QComboBox();
	m_specializationScope->setObjectName(QStringLiteral("swift_specialization_scope"));
	// userData carries the wire value; the visible label is human-friendly.
	m_specializationScope->addItem(QStringLiteral("Off"), QStringLiteral("off"));
	m_specializationScope->addItem(QStringLiteral("Local"), QStringLiteral("local"));
	m_specializationScope->addItem(QStringLiteral("All"), QStringLiteral("all"));

	layout->addWidget(
		createFormLabel(QStringLiteral("Type Argument Nodes")),
		row,
		QtProjectWizardWindow::FRONT_COL,
		Qt::AlignRight);
	layout->addWidget(m_specializationScope, row, QtProjectWizardWindow::BACK_COL);
	addHelpButton(
		QStringLiteral("Type Argument Nodes"),
		"<p>Whether generic use sites (<b>Base&lt;Arg&gt;</b>) get a type-argument edge from "
		"the enclosing declaration to each argument type.</p>"
		"<p><b>Off</b>: no type-argument edges &mdash; arguments stay plain type usages.<br>"
		"<b>Local</b> (default): edges only for generic types defined in your package or its "
		"dependencies; stdlib containers (<b>Array</b>, <b>Optional</b>, <b>Dictionary</b>) are "
		"skipped to keep the graph readable.<br>"
		"<b>All</b>: edges for every generic instantiation, including the standard library.</p>",
		layout,
		row);
	row++;
}

void QtProjectWizardContentSwiftOptions::load()
{
	m_buildArgs->setText(
		QString::fromStdString(utility::join(m_settings->getSwiftBuildArgs(), " ")));
	m_toolchainPath->setText(
		QString::fromStdString(m_settings->getSwiftToolchainPath().str()));
	m_indexStorePath->setText(
		QString::fromStdString(m_settings->getSwiftIndexStorePath().str()));

	const QString scope = QString::fromStdString(m_settings->getSwiftSpecializationScope());
	int scopeIndex = m_specializationScope->findData(scope);
	if (scopeIndex < 0)
	{
		scopeIndex = m_specializationScope->findData(QStringLiteral("local"));
	}
	m_specializationScope->setCurrentIndex(scopeIndex);
}

void QtProjectWizardContentSwiftOptions::save()
{
	std::vector<std::string> buildArgs;
	for (const std::string& part :
		 utility::splitToVector(m_buildArgs->text().toStdString(), ' '))
	{
		for (const std::string& arg : utility::splitToVector(part, ','))
		{
			const std::string trimmed = utility::trim(arg);
			if (!trimmed.empty())
			{
				buildArgs.push_back(trimmed);
			}
		}
	}
	m_settings->setSwiftBuildArgs(buildArgs);
	m_settings->setSwiftToolchainPath(
		FilePath(utility::trim(m_toolchainPath->getText().toStdString())));
	m_settings->setSwiftIndexStorePath(
		FilePath(utility::trim(m_indexStorePath->getText().toStdString())));
	m_settings->setSwiftSpecializationScope(
		m_specializationScope->currentData().toString().toStdString());
}
