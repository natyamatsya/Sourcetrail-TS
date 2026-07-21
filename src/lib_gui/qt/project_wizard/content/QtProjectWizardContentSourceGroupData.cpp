#include "QtProjectWizardContentSourceGroupData.h"
#include "QtMessageBox.h"

#include <QCheckBox>
#include <QLineEdit>

#ifndef SRCTRL_MODULE_BUILD
#include "SourceGroupSettings.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

QtProjectWizardContentSourceGroupData::QtProjectWizardContentSourceGroupData(
	std::shared_ptr<SourceGroupSettings> settings, QtProjectWizardWindow* window)
	: QtProjectWizardContent(window), m_settings(settings) 
{
	setIsRequired(true);
}

void QtProjectWizardContentSourceGroupData::populate(QGridLayout* layout, int& row)
{
	m_name = new QLineEdit();
	m_name->setObjectName(QStringLiteral("name"));
	m_name->setAttribute(Qt::WA_MacShowFocusRect, false);
	connect(m_name, &QLineEdit::textEdited, this, &QtProjectWizardContentSourceGroupData::editedName);

	layout->addWidget(
		createFormLabel(QStringLiteral("Source Group Name")),
		row,
		QtProjectWizardWindow::FRONT_COL,
		Qt::AlignRight);
	layout->addWidget(m_name, row, QtProjectWizardWindow::BACK_COL);
	row++;

	m_status = new QCheckBox(QStringLiteral("active"));
	connect(
		m_status, &QCheckBox::toggled, this, &QtProjectWizardContentSourceGroupData::changedStatus);
	layout->addWidget(
		createFormSubLabel(QStringLiteral("Status")),
		row,
		QtProjectWizardWindow::FRONT_COL,
		Qt::AlignRight);
	layout->addWidget(m_status, row, QtProjectWizardWindow::BACK_COL);

	addHelpButton(
		QStringLiteral("Status"),
		"<p>Only source files of active Source Groups will be processed during indexing. "
		"Inactive Source Groups will be ignored or cleared from the index.</p><p>Use this setting "
		"to temporarily "
		"remove files from your project (e.g. tests).</p>",
		layout,
		row);

	row++;
}

void QtProjectWizardContentSourceGroupData::load()
{
	m_name->setText(QString::fromStdString(m_settings->getName()));

	m_status->setChecked(m_settings->getStatus() == SourceGroupStatusType::ENABLED);
}

void QtProjectWizardContentSourceGroupData::save()
{
	m_settings->setName(m_name->text().toStdString());

	m_settings->setStatus(
		m_status->isChecked() ? SourceGroupStatusType::ENABLED : SourceGroupStatusType::DISABLED);
}

bool QtProjectWizardContentSourceGroupData::check()
{
	if (m_name->text().isEmpty())
	{
		QtMessageBox msgBox(m_window);
		msgBox.setText(QStringLiteral("Please enter a source group name."));
		msgBox.execModal();
		return false;
	}

	return true;
}

void QtProjectWizardContentSourceGroupData::editedName(QString name)
{
	if (!m_status->isChecked())
	{
		name = "(" + name + ")";
	}

	emit nameUpdated(name);
}

void QtProjectWizardContentSourceGroupData::changedStatus(bool  /*checked*/)
{
	emit statusUpdated(
		m_status->isChecked() ? SourceGroupStatusType::ENABLED : SourceGroupStatusType::DISABLED);

	editedName(m_name->text());
}
