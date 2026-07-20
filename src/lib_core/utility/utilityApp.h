#ifndef UTILITY_APP_H
#define UTILITY_APP_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <chrono>
#include <string>
#include <vector>

#include "FilePath.h"
#include "Platform.h"
#endif

SRCTRL_EXPORT namespace utility
{
// inline: namespace-scope constexpr would otherwise have internal linkage, which cannot be exported
// from a module.
inline constexpr std::chrono::milliseconds DEFAULT_TIMEOUT(30000);
inline constexpr std::chrono::milliseconds INFINITE_TIMEOUT(std::chrono::milliseconds::max());

struct ProcessOutput
{
	std::string output;
	std::string error;
	int exitCode;
	std::string processInfo;
};

std::string getDocumentationLink();

std::string searchPath(const std::string& bin, bool& ok);

std::string searchPath(const std::string& bin);

ProcessOutput executeProcess(
	const std::string& command,
	const std::vector<std::string>& arguments,
	const FilePath& workingDirectory = FilePath(),
	const bool waitUntilNoOutput = false,
	const std::chrono::milliseconds &timeout = DEFAULT_TIMEOUT,
	bool logProcessOutput = false);

void killRunningProcesses();

int getIdealThreadCount();

}

#include "utilityApp.inl"

#endif	  // UTILITY_APP_H
