// `srctrl.logging` -- the full logging subsystem as a module: the backend (LogMessage, the Logger
// interface, ConsoleLogger, FileLogger, LogManagerImplementation, the LogManager singleton) plus the
// module-native srctrl::log front end (LogFacade). Deliberately at the BOTTOM of the module stack --
// no srctrl.* imports (srctrl.file itself imports this module), which is why FileLogger speaks
// std::filesystem::path instead of FilePath.
//
// Seams kept classic: logging.h's LOG_* macros (macros don't cross import -- wrappers include
// logging.h AFTER #define SRCTRL_MODULE_PURVIEW so only the macro definitions survive, and the
// expansions name the imported LogManager), and the MessageStatus/Version toggle announcement
// (LogManagerNotifier.h in the GMF here, defined in classic LogManagerNotifier.cpp -- messaging
// stays out of the module graph).

module;

#ifndef SRCTRL_IMPORT_STD
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#endif

// The include-free messaging-seam declaration (classic definition in LogManagerNotifier.cpp); its
// include guard makes LogManager.h's own include of it a no-op inside the purview, keeping the
// declaration a global-module entity.
#include "LogManagerNotifier.h"

export module srctrl.logging;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// Class definitions in dependency order, then the inline bodies (each header's own bottom-include is
// purview-guarded).
#include "LogMessage.h"
#include "Logger.h"
#include "LogManagerImplementation.h"
#include "LogManager.h"
#include "ConsoleLogger.h"
#include "FileLogger.h"
#include "LogFacade.h"

#include "Logger.inl"
#include "LogManagerImplementation.inl"
#include "LogManager.inl"
#include "ConsoleLogger.inl"
#include "FileLogger.inl"

// close the purview-wide extern "C++" linkage block
}
