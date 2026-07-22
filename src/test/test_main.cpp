#include "Catch2.hpp"

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#endif
#include "utilityPathDetection.h"
#ifndef SRCTRL_MODULE_BUILD
#include <AppPath.h>
#include <UserPaths.h>
#endif
#include <setupApp.h>

#include "language_packages.h"

#if BUILD_CXX_LANGUAGE_PACKAGE
#ifndef SRCTRL_MODULE_BUILD
#include "CxxIndexerCommandCodec.h"
#endif
#endif

#include <filesystem>
#include <iostream>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
#if BUILD_CXX_LANGUAGE_PACKAGE
import srctrl.cxx;   // only built when the C/C++ indexer package is on (matches the include above)
#endif
import srctrl.file;
import srctrl.settings;
#endif

using namespace std;
using namespace std::filesystem;
using namespace utility;

struct EventListener : Catch2::EventListenerBase
{
	static int s_argc;
	static char **s_argv;
	// Absolute app directory, resolved in main() from argv[0] *before* the
	// working directory is changed (below). Recomputing it here from the
	// possibly-relative argv[0] would be wrong: this listener runs after main()
	// has already cd'd into the binary's directory, so a relative argv[0] no
	// longer resolves and getCanonical() would yield a bogus path — which left
	// AppPath::getRustIndexerFilePath() pointing at a nonexistent location and
	// the TaskBuildIndex subprocess tests never launching their indexer.
	static string s_appPath;

	using Catch2::EventListenerBase::EventListenerBase;	   // inherit constructor

	void testRunStarting(const Catch::TestRunInfo& ) override
	{
		FilePath appPath(s_appPath);
		cout << "Setting 'app' directory to " << appPath.str() << endl;
		setupAppDirectories(appPath);

		FilePath settingsFilePath = UserPaths::getAppSettingsFilePath();
		cout << "Loading settings from " << settingsFilePath.str() << endl;
		ApplicationSettings::getInstance()->load(settingsFilePath, true);

#if BUILD_CXX_LANGUAGE_PACKAGE
		// Register the Cxx indexer-command codec, exactly as app/main.cpp and
		// indexer/main.cpp do. It lives in lib_cxx and can't self-register from
		// lib_core's registry (that would be an upward dependency), nor via a
		// static initializer (the linker strips the unreferenced TU from the
		// static archive). The IPC serializer round-trip tests need it present.
		registerCxxIndexerCommandCodec();
#endif
	}
};

int EventListener::s_argc = 0;
char **EventListener::s_argv = nullptr;
string EventListener::s_appPath;

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
	// Catch2 writes its machine readable listings (used by catch_discover_tests()) to the
	// possibly locale imbued cout, which makes the emitted JSON unparsable with a locale
	// that uses digit grouping (e.g. "rng-seed": 2,022,882,781 with a German locale). The
	// UNIX builds prevent this with the run_with_c_locale.sh wrapper, but MSVC ignores
	// LC_ALL, so skip the locale setup when only a listing was requested:
	bool isListing = false;
	for (int i = 1; i < argc; ++i)
		if (string_view(argv[i]).starts_with("--list-"))
			isListing = true;

	if (!isListing)
		setupDefaultLocale();

	// Set the 'working directory' manually, as a workaround for "Unable to configure working directory
	// in CMake/Catch" (https://github.com/catchorg/Catch2/issues/2249)
	path workingDirectory = canonical(path(argv[0])).parent_path();

	// Resolve the app directory (sibling of the binary's directory) to an
	// ABSOLUTE path now, while argv[0] still resolves against the original
	// working directory — before current_path() is changed below. The listener
	// consumes this; recomputing it there from a relative argv[0] would break.
	EventListener::s_appPath = (workingDirectory.parent_path() / "app").string();

	// If something is printed to the screen, then this will lead to a failure in 'catch_discover_tests()'!
	// cout << "Set working directory to '" << workingDirectory << "'" << endl;
	current_path(workingDirectory);

	return catch2_main(argc, argv);
}
