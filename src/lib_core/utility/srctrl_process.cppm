// `srctrl.process` -- process-execution utilities (utilityApp: executeProcess / searchPath /
// killRunningProcesses / getIdealThreadCount). Module build only.
//
// Its own module rather than a partition: it is the one mid-layer piece that couples Qt (QProcess)
// with FilePath, so it cannot join srctrl.file (deliberately Qt-free) nor srctrl.qt (a pure Qt
// value-type wrapper with no first-party imports).

module;

// GMF: the Qt process machinery (incl. the QStringLiteral macro the .inl uses), ScopedFunctor
// (std-only header), Platform, and the logging macros.
#ifndef SRCTRL_IMPORT_STD
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <vector>
#endif

#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>

#include "Platform.h"
#include "ScopedFunctor.h"

export module srctrl.process;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.file;      // FilePath (executeProcess working directory)
import srctrl.utility;   // utilityString (splitToVector, trim)
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros

#define SRCTRL_MODULE_PURVIEW
// LOG_* macro definitions only (in the purview the header strips its backend includes); the
// expansions name the LogManager imported from srctrl.logging.
#include "logging.h"
#include "utilityApp.h"
