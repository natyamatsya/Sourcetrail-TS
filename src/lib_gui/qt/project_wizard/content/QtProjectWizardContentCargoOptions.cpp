#include "QtProjectWizardContentCargoOptions.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>

#include "SourceGroupSettingsWithCargoOptions.h"
#include "utilityString.h"

QtProjectWizardContentCargoOptions::QtProjectWizardContentCargoOptions(
	std::shared_ptr<SourceGroupSettingsWithCargoOptions> settings, QtProjectWizardWindow* window)
	: QtProjectWizardContent(window), m_settings(settings)
{
}

void QtProjectWizardContentCargoOptions::populate(QGridLayout* layout, int& row)
{
	m_features = new QLineEdit();
	m_features->setObjectName(QStringLiteral("cargo_features"));
	m_features->setAttribute(Qt::WA_MacShowFocusRect, false);
	m_features->setPlaceholderText(QStringLiteral("feature-a feature-b"));

	layout->addWidget(
		createFormLabel(QStringLiteral("Cargo Features")),
		row,
		QtProjectWizardWindow::FRONT_COL,
		Qt::AlignRight);
	layout->addWidget(m_features, row, QtProjectWizardWindow::BACK_COL);
	addHelpButton(
		QStringLiteral("Cargo Features"),
		"<p>Cargo features to enable while indexing (space or comma separated), matching "
		"<b>cargo build --features</b>. Feature-gated code that is not enabled is invisible to "
		"the index.</p>",
		layout,
		row);
	row++;

	m_allFeatures = new QCheckBox(QStringLiteral("enable all features (--all-features)"));
	layout->addWidget(m_allFeatures, row, QtProjectWizardWindow::BACK_COL);
	row++;

	m_noDefaultFeatures = new QCheckBox(
		QStringLiteral("disable default features (--no-default-features)"));
	layout->addWidget(m_noDefaultFeatures, row, QtProjectWizardWindow::BACK_COL);
	row++;

	m_targetTriple = new QLineEdit();
	m_targetTriple->setObjectName(QStringLiteral("cargo_target_triple"));
	m_targetTriple->setAttribute(Qt::WA_MacShowFocusRect, false);
	m_targetTriple->setPlaceholderText(QStringLiteral("host"));

	layout->addWidget(
		createFormLabel(QStringLiteral("Target Triple")),
		row,
		QtProjectWizardWindow::FRONT_COL,
		Qt::AlignRight);
	layout->addWidget(m_targetTriple, row, QtProjectWizardWindow::BACK_COL);
	addHelpButton(
		QStringLiteral("Target Triple"),
		"<p>rustc target triple to resolve <b>cfg(target_os = ...)</b> and friends against "
		"(e.g. <b>x86_64-unknown-linux-gnu</b>). Leave empty to use the host target.</p>",
		layout,
		row);
	row++;

	m_specializationScope = new QComboBox();
	m_specializationScope->setObjectName(QStringLiteral("rust_specialization_scope"));
	// userData carries the wire value; the visible label is human-friendly.
	m_specializationScope->addItem(QStringLiteral("Off"), QStringLiteral("off"));
	m_specializationScope->addItem(QStringLiteral("Local"), QStringLiteral("local"));
	m_specializationScope->addItem(QStringLiteral("All"), QStringLiteral("all"));

	layout->addWidget(
		createFormLabel(QStringLiteral("Specialization Nodes")),
		row,
		QtProjectWizardWindow::FRONT_COL,
		Qt::AlignRight);
	layout->addWidget(m_specializationScope, row, QtProjectWizardWindow::BACK_COL);
	addHelpButton(
		QStringLiteral("Specialization Nodes"),
		"<p>Whether generic use sites (<b>Base&lt;Arg&gt;</b>) get their own implicit "
		"specialization node in the graph.</p>"
		"<p><b>Off</b>: no bubbles &mdash; direct type-argument edges from the enclosing "
		"item (pre-feature graph).<br>"
		"<b>Local</b> (default): bubble only for your crate's own generic types; external "
		"bases (<b>Vec</b>, <b>Option</b>) keep the plain edges.<br>"
		"<b>All</b>: bubble every generic instantiation (full fidelity, more nodes).</p>",
		layout,
		row);
	row++;
}

void QtProjectWizardContentCargoOptions::load()
{
	m_features->setText(
		QString::fromStdString(utility::join(m_settings->getCargoFeatures(), " ")));
	m_allFeatures->setChecked(m_settings->getCargoAllFeatures());
	m_noDefaultFeatures->setChecked(m_settings->getCargoNoDefaultFeatures());
	m_targetTriple->setText(QString::fromStdString(m_settings->getCargoTargetTriple()));

	const QString scope = QString::fromStdString(m_settings->getRustSpecializationScope());
	int scopeIndex = m_specializationScope->findData(scope);
	if (scopeIndex < 0)
	{
		scopeIndex = m_specializationScope->findData(QStringLiteral("local"));
	}
	m_specializationScope->setCurrentIndex(scopeIndex);
}

void QtProjectWizardContentCargoOptions::save()
{
	std::vector<std::string> features;
	for (const std::string& part :
		 utility::splitToVector(m_features->text().toStdString(), ' '))
	{
		for (const std::string& feature : utility::splitToVector(part, ','))
		{
			const std::string trimmed = utility::trim(feature);
			if (!trimmed.empty())
			{
				features.push_back(trimmed);
			}
		}
	}
	m_settings->setCargoFeatures(features);
	m_settings->setCargoAllFeatures(m_allFeatures->isChecked());
	m_settings->setCargoNoDefaultFeatures(m_noDefaultFeatures->isChecked());
	m_settings->setCargoTargetTriple(utility::trim(m_targetTriple->text().toStdString()));
	m_settings->setRustSpecializationScope(
		m_specializationScope->currentData().toString().toStdString());
}
