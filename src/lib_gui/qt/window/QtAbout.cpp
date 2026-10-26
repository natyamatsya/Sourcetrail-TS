#include "QtAbout.h"

#include "QtDeviceScaledPixmap.h"
#include "SqliteIndexStorage.h"
#include "Version.h"
#include "utilityQt.h"
#include "QtResources.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace utility;
using namespace std::string_literals;

QtAbout::QtAbout(QWidget *parent)
	: QtWindow(false, parent)
{
	setWindowTitle(tr("About Sourcetrail"));
}

QSize QtAbout::sizeHint() const
{
	return QSize(450, 480);
}

void QtAbout::setupAbout()
{
	setStyleSheet(QtResources::loadStyleSheet(QtResources::ABOUT_CSS));

	QVBoxLayout *windowLayout = new QVBoxLayout();
	windowLayout->setContentsMargins(10, 10, 10, 0);
	windowLayout->setSpacing(1);
	m_content->setLayout(windowLayout);

	{
		QtDeviceScaledPixmap sourcetrailLogo(QString::fromUtf8(QtResources::ABOUT_LOGO_SOURCETRAIL));
		sourcetrailLogo.scaleToHeight(150);
		QLabel *sourcetrailLogoLabel = new QLabel(this);
		sourcetrailLogoLabel->setPixmap(sourcetrailLogo.pixmap());
		sourcetrailLogoLabel->resize(static_cast<int>(sourcetrailLogo.width()), static_cast<int>(sourcetrailLogo.height()));
		windowLayout->addWidget(sourcetrailLogoLabel, 0, Qt::Alignment(Qt::AlignmentFlag::AlignHCenter));
	}

	windowLayout->addSpacing(10);

	{
		QLabel *versionLabel = new QLabel(tr("Version %1").arg(QString::fromStdString(Version::getApplicationVersion().toDisplayString())), this);
		windowLayout->addWidget(versionLabel, 0, Qt::Alignment(Qt::AlignmentFlag::AlignHCenter));
	}

	{
		QLabel *dbVersionLabel = new QLabel(tr("Database Version %1").arg(SqliteIndexStorage::getStorageVersion()), this);
		windowLayout->addWidget(dbVersionLabel, 0, Qt::Alignment(Qt::AlignmentFlag::AlignHCenter));
	}

	windowLayout->addStretch();

	{
		QHBoxLayout *layoutHorz1 = new QHBoxLayout();
		windowLayout->addLayout(layoutHorz1);

		layoutHorz1->addStretch();

		QLabel* developerLabel = new QLabel(
			"<b>Authors:</b><br />"
			"<a href=\"https://github.com/petermost/Sourcetrail/blob/master/docs/COATI_AUTHORS.txt\""
			"style=\"color: white;\">The Coati Software Developer</a><br />"
			"<br />"
			"<b>Maintainer:</b><br />"
			"Peter Most<br />"
			"<a href=\"https://www.pera-software.com\""
			"style=\"color: white;\">PERA Software Solutions GmbH</a><br />"
			"<br />"
			"<b>Repository:</b><br />"
			"<a href=\"http://sourcetrail.de\""
			"style=\"color: white;\">sourcetrail.de</a><br />");

		developerLabel->setObjectName(QStringLiteral("small"));
		developerLabel->setOpenExternalLinks(true);
		layoutHorz1->addWidget(developerLabel);

		layoutHorz1->addStretch();
	}

	windowLayout->addStretch();
}
