#include "ToolChain.h"
#include "language_packages.h"

#include <optional>   // was transitive via utility.h before the import conversion

#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#endif

#if BUILD_CXX_LANGUAGE_PACKAGE
	#include <llvm/Config/llvm-config.h>
#endif

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.utility;
#endif

// Note: We do the '#if LLVM_VERSION_MAJOR ==' check in case we need to support two different clang
// versions in the system build and the vcpkg build!
//
// From llvm-config.h:
// #define LLVM_VERSION_MAJOR 18
// #define LLVM_VERSION_MAJOR 19
//
// C++
// /usr/bin/clang-20 -std=xxx empty.cpp
// vcpkg_installed/x64-arm64-linux-windows-osx-static-md/tools/llvm/clang-18 -std=xxx empty.cpp
//
// Released standards:
// note: use 'c++98' or 'c++03' for 'ISO C++ 1998 with amendments' standard
// note: use 'gnu++98' or 'gnu++03' for 'ISO C++ 1998 with amendments and GNU extensions' standard
// note: use 'c++11' for 'ISO C++ 2011 with amendments' standard
// note: use 'gnu++11' for 'ISO C++ 2011 with amendments and GNU extensions' standard
// note: use 'c++14' for 'ISO C++ 2014 with amendments' standard
// note: use 'gnu++14' for 'ISO C++ 2014 with amendments and GNU extensions' standard
// note: use 'c++17' for 'ISO C++ 2017 with amendments' standard
// note: use 'gnu++17' for 'ISO C++ 2017 with amendments and GNU extensions' standard
// note: use 'c++20' for 'ISO C++ 2020 DIS' standard
// note: use 'gnu++20' for 'ISO C++ 2020 DIS with GNU extensions' standard
// note: use 'c++23' for 'ISO C++ 2023 DIS' standard
// note: use 'gnu++23' for 'ISO C++ 2023 DIS with GNU extensions' standard
//
// Draft standards:
// note: use 'c++2c' or 'c++26' for 'Working draft for C++2c' standard
// note: use 'gnu++2c' or 'gnu++26' for 'Working draft for C++2c with GNU extensions' standard

static std::vector<std::string> getReleasedCppStandards()
{
	const std::vector<std::string> releasedCppStandards = {
		#ifdef LLVM_VERSION_MAJOR
			#if LLVM_VERSION_MAJOR >= 18
				"c++23", "gnu++23",
				"c++20", "gnu++20",
				"c++17", "gnu++17",
				"c++14", "gnu++14",
				"c++11", "gnu++11",
				"c++03", "gnu++03",
				"c++98", "gnu++98",
			#endif
		#endif
	};
	return releasedCppStandards;
}

static std::vector<std::string> getDraftCppStandards()
{
	const std::vector<std::string> draftCppStandards = {
		#ifdef LLVM_VERSION_MAJOR
			#if LLVM_VERSION_MAJOR >= 18
				"c++2c", "c++26",
				"gnu++2c", "gnu++26",
			#endif
		#endif
	};
	return draftCppStandards;
}

// C
// /usr/bin/clang-20 -std=xxx empty.c
// vcpkg_installed/x64-arm64-linux-windows-osx-static-md/tools/llvm/clang-18 -std=xxx empty.c
//
// Released standards:
// note: use 'c89', 'c90', or 'iso9899:1990' for 'ISO C 1990' standard
// note: use 'iso9899:199409' for 'ISO C 1990 with amendment 1' standard
// note: use 'gnu89' or 'gnu90' for 'ISO C 1990 with GNU extensions' standard
// note: use 'c99' or 'iso9899:1999' for 'ISO C 1999' standard
// note: use 'gnu99' for 'ISO C 1999 with GNU extensions' standard
// note: use 'c11' or 'iso9899:2011' for 'ISO C 2011' standard
// note: use 'gnu11' for 'ISO C 2011 with GNU extensions' standard
// note: use 'c17', 'iso9899:2017', 'c18', or 'iso9899:2018' for 'ISO C 2017' standard
// note: use 'gnu17' or 'gnu18' for 'ISO C 2017 with GNU extensions' standard
//
// Draft standards:
// note: use 'c23' for 'Working Draft for ISO C23' standard
// note: use 'gnu23' for 'Working Draft for ISO C23 with GNU extensions' standard
// note: use 'c2y' for 'Working Draft for ISO C2y' standard
// note: use 'gnu2y' for 'Working Draft for ISO C2y with GNU extensions' standard

static std::vector<std::string> getReleasedCStandards()
{
	const std::vector<std::string> releasedCStandards = {
		#ifdef LLVM_VERSION_MAJOR
			#if LLVM_VERSION_MAJOR >= 18
				"c17", "gnu17",
				"c11", "gnu11",
				"c99", "gnu99",
				"c89", "gnu89",
			#endif
		#endif
	};
	return releasedCStandards;
}

static std::vector<std::string> getDraftCStandards()
{
	const std::vector<std::string> draftCStandards = {
		#ifdef LLVM_VERSION_MAJOR
			#if LLVM_VERSION_MAJOR == 18 || LLVM_VERSION_MAJOR == 19
				"c23", "gnu23"
			#endif
			#if LLVM_VERSION_MAJOR >= 19
				"c2y", "gnu2y"
			#endif
		#endif
	};
	return draftCStandards;
}

///////////////////////////////////////////////////////////////////////////////
//
// Clang:
//
///////////////////////////////////////////////////////////////////////////////

std::string ClangCompiler::verboseOption()
{
	return "-v";
}

std::string ClangCompiler::stdOption(const std::string &languageVersion)
{
	return "-std=" + languageVersion;
}

std::string ClangCompiler::stdCOption(const std::string &version)
{
	return stdOption("c" + version);
}

std::string ClangCompiler::stdCppOption(const std::string &version)
{
	return stdOption("c++" + version);
}

std::string ClangCompiler::pthreadOption()
{
	return"-pthread";
}

std::string ClangCompiler::compileOption()
{
	// This option signals that no executable is built.

	return "-c";
}

std::string ClangCompiler::syntaxOnlyOption()
{
	return "-fsyntax-only";
}


std::string ClangCompiler::msExtensionsOption()
{
	return "-fms-extensions";
}

std::string ClangCompiler::msCompatibilityOption()
{
	return "-fms-compatibility";
}

std::string ClangCompiler::msCompatibilityVersionOption(const std::string &version)
{
	return "-fms-compatibility-version=" + version;
}

std::string ClangCompiler::preprocessOption()
{
	return "-E";
}

std::string ClangCompiler::defineOption(const std::string &nameValue)
{
	return "-D" + nameValue;
}

std::string ClangCompiler::undefineOption(const std::string &name)
{
	return "-U" + name;
}

std::string ClangCompiler::includeOption()
{
	return "-I";
}

std::string ClangCompiler::includeOption(const std::string &directory)
{
	// -I<dir>, --include-directory <arg>, --include-directory=<arg>

	return includeOption() + directory;
}

std::string ClangCompiler::forceIncludeOption()
{
	return "-include";
}

std::string ClangCompiler::forceIncludeOption2()
{
	return "--include";
}

std::string ClangCompiler::forceIncludeOption(const std::string &file)
{
	// -include<file>, --include<file>, --include=<arg>

	return forceIncludeOption() + file;
}

std::string ClangCompiler::systemIncludeOption()
{
	return "-isystem";
}

std::string ClangCompiler::systemIncludeOption(const std::string &directory)
{
	// -isystem<directory>

	return systemIncludeOption() + directory;
}

std::string ClangCompiler::frameworkIncludeOption()
{
	return "-iframework";
}

std::string ClangCompiler::frameworkIncludeOption(const std::string &directory)
{
	// -iframework<arg>

	return frameworkIncludeOption() + directory;
}

std::string ClangCompiler::quoteIncludeOption()
{
	return "-iquote";
}

std::string ClangCompiler::outputOption()
{
	return "-o";
}

std::string ClangCompiler::noWarningsOption()
{
	// This option disables all warnings.

	return "-w";
}

std::string ClangCompiler::emitPchOption()
{
	return "-emit-pch";
}

std::string ClangCompiler::includePchOption()
{
	return "-include-pch";
}

std::string ClangCompiler::allowPchWithCompilerErrors()
{
	return "-fallow-pch-with-compiler-errors";
}

std::string ClangCompiler::errorLimitOption(int limit)
{
	// This option tells clang just to continue parsing no matter how manny errors have been thrown.

	return "-ferror-limit=" + std::to_string(limit);
}

std::string ClangCompiler::exceptionsOption()
{
	// This option signals that clang should watch out for exception-related code during indexing.

	return "-fexceptions";
}

std::string ClangCompiler::noDelayedTemplateParsingOption()
{
	// This option signals that templates that there should be AST elements for unused template functions as well.

	return "-fno-delayed-template-parsing";
}

std::string ClangCompiler::languageOption()
{
	return "-x";
}

std::string ClangCompiler::targetOption(const std::string &target)
{
	return "--target=" + target;
}

std::string ClangCompiler::getLatestCppStandard()
{
	return getReleasedCppStandards()[0];
}

std::string ClangCompiler::getLatestCppDraft()
{
	return getDraftCppStandards()[0];
}

std::vector<std::string> ClangCompiler::getAvailableCppStandards()
{
	return utility::concat(getDraftCppStandards(), getReleasedCppStandards());
}

std::string ClangCompiler::getLatestCStandard()
{
	return getReleasedCStandards()[0];
}

std::string ClangCompiler::getLatestCDraft()
{
	return getDraftCStandards()[0];
}

std::vector<std::string> ClangCompiler::getAvailableCStandards()
{
	return utility::concat(getDraftCStandards(), getReleasedCStandards());
}

std::vector<std::string> ClangCompiler::getAvailableArchTypes()
{
	// as defined in llvm/lib/Support/Triple.cpp

	return {
		"aarch64",
		"aarch64_be",
		"aarch64_32",
		"arm",
		"armeb",
		"arc",
		"avr",
		"bpfel",
		"bpfeb",
		"hexagon",
		"mips",
		"mipsel",
		"mips64",
		"mips64el",
		"msp430",
		"powerpc64",
		"powerpc64le",
		"powerpc",
		"r600",
		"amdgcn",
		"riscv32",
		"riscv64",
		"sparc",
		"sparcv9",
		"sparcel",
		"s390x",
		"tce",
		"tcele",
		"thumb",
		"thumbeb",
		"i386",
		"x86_64",
		"xcore",
		"nvptx",
		"nvptx64",
		"le32",
		"le64",
		"amdil",
		"amdil64",
		"hsail",
		"hsail64",
		"spir",
		"spir64",
		"kalimba",
		"lanai",
		"shave",
		"wasm32",
		"wasm64",
		"renderscript32",
		"renderscript64",
	};
}

std::vector<std::string> ClangCompiler::getAvailableVendorTypes()
{
	return {
		"unknown",
		"apple",
		"pc",
		"scei",
		"bgp",
		"bgq",
		"fsl",
		"ibm",
		"img",
		"mti",
		"nvidia",
		"csr",
		"myriad",
		"amd",
		"mesa",
		"suse",
		"oe",
	};
}

std::vector<std::string> ClangCompiler::getAvailableOsTypes()
{
	return {
		"unknown",
		"cloudabi",
		"darwin",
		"dragonfly",
		"freebsd",
		"fuchsia",
		"ios",
		"kfreebsd",
		"linux",
		"lv2",
		"macosx",
		"netbsd",
		"openbsd",
		"solaris",
		"windows",
		"haiku",
		"minix",
		"rtems",
		"nacl",
		"cnk",
		"aix",
		"cuda",
		"nvcl",
		"amdhsa",
		"ps4",
		"elfiamcu",
		"tvos",
		"watchos",
		"mesa3d",
		"contiki",
		"amdpal",
		"hermit",
		"hurd",
		"wasi",
		"emscripten",
	};
}

std::vector<std::string> ClangCompiler::getAvailableEnvironmentTypes()
{
	return {
		"unknown",
		"gnu",
		"gnuabin32",
		"gnuabi64",
		"gnueabihf",
		"gnueabi",
		"gnux32",
		"code16",
		"eabi",
		"eabihf",
		"elfv1",
		"elfv2",
		"android",
		"musl",
		"musleabi",
		"musleabihf",
		"msvc",
		"itanium",
		"cygnus",
		"coreclr",
		"simulator",
		"macabi",
	};
}

///////////////////////////////////////////////////////////////////////////////
//
// Visual Studio:
//
///////////////////////////////////////////////////////////////////////////////

std::vector<std::string> VisualStudio::getVersionRanges()
{
	// Version table: https://github.com/microsoft/vswhere/wiki/Versions#release-versions
	// Sorted from newest to oldest:
	const std::vector<std::string> releasedVisualStudioVersionRanges = {
		"[18.0, 19.0)", // 2026
		"[17.0, 18.0)", // 2022
		"[16.0, 17.0)", // 2019
		"[15.0, 16.0)"  // 2017
	};
	return releasedVisualStudioVersionRanges;
}

std::string VisualStudio::getLatestMsvcVersion()
{
	// Can be found by calling 'cl /?':
	return "19.44"; // TODO (petermost): 19.50?
}

///////////////////////////////////////////////////////////////////////////////
//
// Windows SDK:
//
///////////////////////////////////////////////////////////////////////////////

std::vector<std::string> WindowsSdk::getVersions()
{
	// Adapted from: https://en.wikipedia.org/wiki/Microsoft_Windows_SDK

	const std::vector<std::string> sdkVersions = {
		"v10",   // Windows Standalone SDK for Windows 10 (Also included in Visual Studio 2015)
		"v8.1A", // Included in Visual Studio 2013
		"v8.1",  // Windows Software Development Kit (SDK) for Windows 8.1
		"v8.0A", // Included in Visual Studio 2012
		"v7.1A", // Included in Visual Studio 2012 Update 1 (or later)
		"v7.0A"  // Included in Visual Studio 2010
	};
	return sdkVersions;
}

///////////////////////////////////////////////////////////////////////////////
//
// MSVC/Clang:
//
///////////////////////////////////////////////////////////////////////////////

static std::optional<std::string> getArgumentValue(const std::string &argument, std::string_view argumentKey)
{
	return argument.starts_with(argumentKey) ? std::optional(argument.substr(argumentKey.length())) : std::nullopt;
}

// MSVC options may be spelled with '-' as well as '/'. The translations above
// handle the '-' forms of the options we keep; this recognizes the MSVC-only
// rest in '-' spelling so it can be dropped like its '/' twin: warning options
// (-wd/-we/-wo/-w1..-w4 followed by a warning number), conformance switches
// (-Zc:...), debug info (-Zi/-Z7/-ZI), -permissive-, and -external:...
// variants (the -external:I include form is already translated above).
static bool isDashSpelledMsvcOnlyArgument(const std::string &argument)
{
	const std::string_view arg{argument};
	if (!arg.starts_with('-'))
		return false;
	if (arg.starts_with("-Zc:") || arg == "-Zi" || arg == "-Z7" || arg == "-ZI" ||
		arg == "-permissive-" || arg == "-utf-8" || arg.starts_with("-external:"))
		return true;
	if (arg.size() > 3 && arg[1] == 'w' &&
		(arg[2] == 'd' || arg[2] == 'e' || arg[2] == 'o' || ('1' <= arg[2] && arg[2] <= '4')))
		return arg.find_first_not_of("0123456789", 3) == std::string_view::npos;
	return false;
}

// - Keep/Replace only those options which are necessary to parse the code correctly
//
// From https://learn.microsoft.com/en-us/cpp/build/reference/compiler-options:
// - All compiler options are case-sensitive.
// - You may use either a forward slash (/) or a dash (-) to specify a compiler option.
static void translateMsvcTokens(std::vector<std::string> *commandLineArguments, std::vector<std::string>::iterator argument)
{
	std::optional<std::string> argumentValue;

	while (argument != commandLineArguments->end())
	{
		// Preprocessor symbols:

		if ((argumentValue = getArgumentValue(*argument, "/D")))
			*argument++ = ClangCompiler::defineOption(*argumentValue);
		else if ((argumentValue = getArgumentValue(*argument, "/U")))
			*argument++ = ClangCompiler::undefineOption(*argumentValue);
		else if ((argumentValue = getArgumentValue(*argument, "/FI")) || (argumentValue = getArgumentValue(*argument, "-FI")))
			*argument++ = ClangCompiler::forceIncludeOption(*argumentValue);

		// Preprocessor include directories:

		else if ((argumentValue = getArgumentValue(*argument, "/I")))
			*argument++ = ClangCompiler::includeOption(*argumentValue);
		else if ((argumentValue = getArgumentValue(*argument, "/external:I")) || (argumentValue = getArgumentValue(*argument, "-external:I")))
			*argument++ = ClangCompiler::systemIncludeOption(*argumentValue);

		// C/C++ language version selection
		// Note: 'latest' and 'preview' must be checked before concrete versions!

		else if (argument->starts_with("/std:c++latest") || argument->starts_with("-std:c++latest"))
			*argument++ = ClangCompiler::stdOption(ClangCompiler::getLatestCppStandard());
		else if (argument->starts_with("/std:clatest") || argument->starts_with("-std:clatest"))
			*argument++ = ClangCompiler::stdOption(ClangCompiler::getLatestCStandard());

		// Only check for 'preview' and ignore the version:
		// Note: There is no 'c<version>preview', but check it anyway

		else if ((argument->starts_with("/std:c++") || argument->starts_with("-std:c++")) && argument->ends_with("preview"))
			*argument++ = ClangCompiler::stdOption(ClangCompiler::getLatestCppDraft());
		else if ((argument->starts_with("/std:c") || argument->starts_with("-std:c")) && argument->ends_with("preview"))
			*argument++ = ClangCompiler::stdOption(ClangCompiler::getLatestCDraft());

		else if ((argumentValue = getArgumentValue(*argument, "/std:c++")) || (argumentValue = getArgumentValue(*argument, "-std:c++")))
			*argument++ = ClangCompiler::stdCppOption(*argumentValue);
		else if ((argumentValue = getArgumentValue(*argument, "/std:c")) || (argumentValue = getArgumentValue(*argument, "-std:c")))
			*argument++ = ClangCompiler::stdCOption(*argumentValue);

		// Multithread support:

		else if (getArgumentValue(*argument, "/MD") || getArgumentValue(*argument, "/MT")
			|| getArgumentValue(*argument, "-MD") || getArgumentValue(*argument, "-MT"))
		{
			// MSVC's runtime selection predefines macros the code may branch on:
			// /MD[d] and /MT[d] define _MT, /MD[d] additionally _DLL, and the
			// 'd' variants _DEBUG. Clang only sets these in cl driver mode, so
			// spell them out for the parser.
			const bool isDll = getArgumentValue(*argument, "/MD") || getArgumentValue(*argument, "-MD");
			const bool isDebugRuntime = argument->ends_with('d');

			*argument++ = ClangCompiler::defineOption("_MT");
			if (isDll)
				argument = std::next(commandLineArguments->insert(argument, ClangCompiler::defineOption("_DLL")));
			if (isDebugRuntime)
				argument = std::next(commandLineArguments->insert(argument, ClangCompiler::defineOption("_DEBUG")));
		}
		// Remove unknown arguments (both '/' and '-' spellings of MSVC-only options):

		else if (argument->starts_with('/') || isDashSpelledMsvcOnlyArgument(*argument))
			argument = commandLineArguments->erase(argument);
		else
			++argument;
	}
}

void replaceMsvcArguments(std::vector<std::string> *commandLineArguments)
{
	// Replace/Remove arguments only if these are for the Microsoft compiler, otherwise the check for '/' will remove Linux paths:

	if (commandLineArguments->empty() || !(*commandLineArguments)[0].ends_with("cl.exe"))
		return;

	translateMsvcTokens(commandLineArguments, std::next(commandLineArguments->begin())); // skip command
}

void translateMsvcCompilerFragments(std::vector<std::string> *compilerFlags)
{
	// Same translation for bare compiler flags (no argv[0]), e.g. the
	// compileCommandFragments of a CMake File API compile group. The caller
	// decides whether the flags come from the Microsoft compiler.

	translateMsvcTokens(compilerFlags, compilerFlags->begin());
}
