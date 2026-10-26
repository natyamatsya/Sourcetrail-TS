#include "setupApp.h"

#include <format>

#ifndef SRCTRL_MODULE_BUILD
#include <AppPath.h>
#include <ApplicationSettings.h>
#include <FilePath.h>
#include <FileSystem.h>
#include <ResourcePaths.h>
#include <UserPaths.h>
#endif
#include <Version.h>
#include <productVersion.h>
#include <qtScaleFactor.h>

#include "logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.settings;
#endif

using namespace std;
using namespace utility;
using namespace std::filesystem;

static void setupUserDirectory(const FilePath &appPath, const FilePath &userDataDir)
{
	// Determine user directory: the one asked for (--user-data-dir, e.g. an isolated
	// profile for an agent-driven instance), else the per-user location.
	if (!userDataDir.empty())
	{
		UserPaths::setUserDataDirectoryPath(FilePath(userDataDir.str() + "/").makeAbsolute());
	}
	else
	{
		QString userDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/";
		UserPaths::setUserDataDirectoryPath(FilePath(userDataLocation.toStdString()).makeAbsolute());
	}

	// Create missing user directory and copy default configurations:
	if (!UserPaths::getUserDataDirectoryPath().exists()) {
		if (const auto created = FileSystem::createDirectories(UserPaths::getUserDataDirectoryPath()); !created)
		{
			LOG_ERROR(std::format(
				"Could not create the user data directory \"{}\": {}",
				UserPaths::getUserDataDirectoryPath().str(),
				created.error().message()));
		}

		// Copy the default application settings as the legacy XML file; it is migrated
		// to the current (JSON) format on first load (see ApplicationSettings::load).
		FileSystem::copyFile(ResourcePaths::getFallbackDirectoryPath().concatenate("ApplicationSettings.xml"),
			UserPaths::getAppSettingsFilePath().replaceExtension("xml"));

		// Copy a default windows settings file:
		FileSystem::copyFile(ResourcePaths::getFallbackDirectoryPath().concatenate("window_settings.ini"),
			UserPaths::getWindowSettingsFilePath());

		// Copy the example/tutorial projects:
		FileSystem::copyDirectory(appPath.getConcatenated("user").concatenate("projects"),
			UserPaths::getUserDataDirectoryPath().concatenate("projects"));

		// Add u+w permissions because the source files may be marked read-only in some Linux distros:
		for (recursive_directory_iterator it(UserPaths::getUserDataDirectoryPath().getPath()); it != recursive_directory_iterator(); ++it) {
			perms currentPermissions = status(*it).permissions();
			permissions(*it, currentPermissions | perms::owner_write);
		}
	}
}

Version setupAppDirectories(const FilePath &appPath, const FilePath &userDataDir)
{
	QCoreApplication::setApplicationName(QStringLiteral("Sourcetrail"));

	Version version(PRODUCT_VERSION_MAJOR, PRODUCT_VERSION_MINOR, PRODUCT_VERSION_PATCH, PRODUCT_VERSION_SUFFIX);
	QCoreApplication::setApplicationVersion(QString::fromStdString(version.toDisplayString()));

	// Note: This functions is called from main in 'main.cpp' *AND* the main in 'test_main.cpp'.
	// If the appPath is incorrect then resource and user-data initialization will fail.
	AppPath::setSharedDataDirectoryPath(appPath);
	AppPath::setCxxIndexerDirectoryPath(appPath);

	setupUserDirectory(appPath, userDataDir);

	return version;
}

void setupAppEnvironment(int  /*argc*/, char*  /*argv*/[])
{
	// This function will be called after setupAppDirectories, so UserPaths::setUserDataDirectoryPath
	// has been initialized and UserPaths::getAppSettingsFilePath will return the correct path.

	// Set QT screen scaling factor
	ApplicationSettings appSettings;
	appSettings.load(UserPaths::getAppSettingsFilePath(), true);

	int autoScaling = appSettings.getScreenAutoScaling();
	if (autoScaling != -1)
	{
		setQtAutoScreenScaleFactorEnabled(autoScaling != 0);
	}

	float scaleFactor = appSettings.getScreenScaleFactor();
	if (scaleFactor > 0.0)
	{
		setQtScaleFactor(scaleFactor);
	}
}

