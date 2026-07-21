// `srctrl.settings` -- the settings stack: ConfigManager (the flat key/value store with TOML/JSON
// backends), Settings (the file-backed base class), and ApplicationSettings (the app-wide
// singleton). Module build only.
//
// This is the last big mid-layer unblock: ApplicationSettings.h was one of the two remaining
// GMF-poison headers (it textually reaches FilePath.h), gating IncludeProcessing and the settings-
// facing lib_cxx conversions.

module;

#ifndef SRCTRL_IMPORT_STD
#include <cmath>
#include <cstddef>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#endif

// Non-modularized GMF deps: the TOML/JSON backends (header-only third-party), GroupType (std-only
// enum header; classic impl linked), Logger (log-level mask constants), and the logging macros.
// utilityUuid stays global-module the same way (classic impl linked -- a purview-textual include
// would module-mangle the references and never resolve against utilityUuid.cpp.o).
// language_package_flags supplies the LANGUAGE_PACKAGE macros to the family bodies in the purview.
#include <glaze/glaze.hpp>
#include <toml++/toml.hpp>

// Qt is never covered by `import std` and QStringLiteral is a macro -- textual in the GMF
// (utilityString/AidKit precedent). Needed by the exclude-filter bodies.
#include <QRegularExpression>
#include <QString>

#include "GroupType.h"
#include "language_package_flags.h"
#include "utilityUuid.h"
#include "ToolChain.h"   // ClangCompiler (C/Cpp-standard mixins); classic impl linked

export module srctrl.settings;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // utility.h helpers (:containers), Status (:types)
import srctrl.file;      // FilePath, TextAccess, UserPaths/ResourcePaths, utilityFile
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros

#define SRCTRL_MODULE_PURVIEW
// LOG_* macro definitions only (in the purview the header strips its backend includes); the
// expansions name the LogManager imported from srctrl.logging.
#include "logging.h"
// Dependency order: the store, the base class, the singleton, then the source-group/project
// settings family. ProjectSettings.h transitively attaches the whole family (headers include
// their inls at the end; family-internal inl includes are unguarded, so include guards +
// inl-after-class ordering resolve the cross-references inside the purview too).
#include "ConfigManager.h"
#include "Settings.h"
#include "ApplicationSettings.h"
#include "SourceGroupSettings.h"
#include "ProjectSettings.h"
// The concrete source-group settings types (and, transitively, WithComponents + every component
// mixin). NOT reachable via ProjectSettings.h -- its factory lives in a classic .cpp precisely to
// keep these out of the inl chain -- so the wrapper attaches them explicitly.
#include "SourceGroupSettingsCEmpty.h"
#include "SourceGroupSettingsCppEmpty.h"
#include "SourceGroupSettingsCustomCommand.h"
#include "SourceGroupSettingsCxxCdb.h"
#include "SourceGroupSettingsCxxCMakeFileAPI.h"
#include "SourceGroupSettingsRustEmpty.h"
#include "SourceGroupSettingsSwiftEmpty.h"
#include "SourceGroupSettingsUnloadable.h"
#include "SourceGroupSettingsZigEmpty.h"
// The factory, last: it names the whole type family.
#include "ProjectSettingsFactory.inl"
