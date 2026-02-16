#include "Catch2.hpp"

#include "ApplicationSettings.h"
#include "language_packages.h"
#include "utilityPathDetection.h"
#include <AppPath.h>
#include <UserPaths.h>
#include <setupApp.h>

#include <filesystem>
#include <iostream>

using namespace std;
using namespace std::filesystem;
using namespace utility;

struct EventListener : Catch2::EventListenerBase
{
	static int s_argc;
	static char **s_argv;

	using Catch2::EventListenerBase::EventListenerBase;	   // inherit constructor

	void testRunStarting(const Catch::TestRunInfo& ) override
	{
		FilePath appPath = FilePath(s_argv[0]).getCanonical().getParentDirectory().getParentDirectory().getConcatenated("app");
		cout << "Setting 'app' directory to " << appPath.str() << endl;
		setupAppDirectories(appPath);

		FilePath settingsFilePath = UserPaths::getAppSettingsFilePath();
		cout << "Loading settings from " << settingsFilePath.str() << endl;
		ApplicationSettings::getInstance()->load(settingsFilePath, true);
	}
};

int EventListener::s_argc = 0;
char **EventListener::s_argv = nullptr;

CATCH_REGISTER_LISTENER(EventListener)

// Adapted from: https://github.com/catchorg/Catch2/blob/devel/docs/own-main.md:
static int catch2_main(int argc, char *argv[])
{
	EventListener::s_argc = argc;
	EventListener::s_argv = argv;

	Catch::Session session;

	// Catch2 version 3.9.1 changed the default run order to random, which breaks a couple of tests.
	// So restore the old behavior and run the tests in declaration order:
	// See https://github.com/petermost/Sourcetrail/issues/62

	session.configData().runOrder = Catch::TestRunOrder::Declared;

	int returnCode = session.applyCommandLine(argc, argv);
	if (returnCode != 0) // Indicates a command line error
		return returnCode;

	return session.run();
}

int main(int argc, char* argv[])
{
	setupDefaultLocale();

	// Set the 'working directory' manually, as a workaround for "Unable to configure working directory
	// in CMake/Catch" (https://github.com/catchorg/Catch2/issues/2249)
	path workingDirectory = canonical(path(argv[0])).parent_path();

	// If something is printed to the screen, then this will lead to a failure in 'catch_discover_tests()'!
	// cout << "Set working directory to '" << workingDirectory << "'" << endl;
	current_path(workingDirectory);

	return catch2_main(argc, argv);
}
