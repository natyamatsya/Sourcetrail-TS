#include "QtProjectWizardContentDetected.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "QtLineGlyph.h"
#include "QtResources.h"

namespace
{
// The wizard is a light window in every colour scheme (window.css): the note's glyph takes
// the colour its muted text has there.
QColor glyphColor()
{
	return QColor{0x5F, 0x63, 0x6B};
}

QLabel* noteLabel(const QString& text, const QString& objectName, QWidget* parent)
{
	QLabel* label{new QLabel(text, parent)};
	label->setObjectName(objectName);
	label->setWordWrap(true);
	label->setTextFormat(Qt::PlainText);
	label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	return label;
}
}	 // namespace

QtProjectWizardContentDetected::QtProjectWizardContentDetected(
	ProjectWizardView::Detection detection, QtProjectWizardWindow* window)
	: QtProjectWizardContent(window), m_detection(std::move(detection))
{
}

void QtProjectWizardContentDetected::populate(QGridLayout* layout, int& row)
{
	QFrame* note{new QFrame(this)};
	note->setObjectName(QStringLiteral("detectedNote"));
	note->setAccessibleName(QStringLiteral("Detected"));
	note->setAttribute(Qt::WA_StyledBackground);

	QVBoxLayout* noteLayout{new QVBoxLayout(note)};
	noteLayout->setContentsMargins(12, 10, 12, 10);
	noteLayout->setSpacing(4);

	{
		QHBoxLayout* header{new QHBoxLayout()};
		header->setSpacing(6);
		QLabel* glyph{new QLabel(note)};
		glyph->setPixmap(QtLineGlyph::icon(QString::fromLatin1(QtResources::GLYPH_FOLDER_DETECT), glyphColor())
							 .pixmap(QtLineGlyph::size, QtLineGlyph::size));
		glyph->setFixedSize(QtLineGlyph::size, QtLineGlyph::size);
		header->addWidget(glyph, 0, Qt::AlignTop);
		header->addWidget(
			noteLabel(
				QStringLiteral("Detected in %1").arg(QString::fromStdString(m_detection.folder)),
				QStringLiteral("detectedTitle"),
				note),
			1);
		noteLayout->addLayout(header);
	}

	for (std::size_t i = 0; i < m_detection.reasons.size(); i++)
	{
		const QString text{QString::fromStdString(m_detection.reasons[i])};
		noteLayout->addWidget(
			i == 0 ? noteLabel(text, QStringLiteral("detectedSummary"), note)
				   : noteLabel(QStringLiteral("• ") + text, QStringLiteral("detectedReason"), note));
	}

	if (!m_detection.notices.empty())
	{
		QFrame* line{new QFrame(note)};
		line->setObjectName(QStringLiteral("detectedLine"));
		line->setFixedHeight(1);
		noteLayout->addSpacing(2);
		noteLayout->addWidget(line);
		noteLayout->addSpacing(2);
		for (const std::string& notice: m_detection.notices)
		{
			noteLayout->addWidget(noteLabel(QString::fromStdString(notice), QStringLiteral("detectedNotice"), note));
		}
	}

	layout->addWidget(
		note,
		row,
		QtProjectWizardWindow::FRONT_COL,
		1,
		1 + QtProjectWizardWindow::BACK_COL - QtProjectWizardWindow::FRONT_COL);
	row++;
}
