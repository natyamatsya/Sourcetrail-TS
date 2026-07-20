// Inline implementations for ConsoleLogger.h. Included at the end of that header (classic) or via
// the srctrl.logging wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <iostream>
#endif

inline ConsoleLogger::ConsoleLogger(): Logger("ConsoleLogger") {}

inline void ConsoleLogger::logInfo(const LogMessage& message)
{
	logMessage("INFO", message);
}

inline void ConsoleLogger::logWarning(const LogMessage& message)
{
	logMessage("WARNING", message);
}

inline void ConsoleLogger::logError(const LogMessage& message)
{
	logMessage("ERROR", message);
}

inline void ConsoleLogger::logMessage(const std::string& type, const LogMessage& message)
{
	std::cout << message.getTimeString("%H:%M:%S") << " | ";

	if (!message.filePath.empty())
	{
		std::cout << message.getFileName() << ':' << message.line << ' ' << message.functionName << "() | ";
	}

	std::cout << type << ": " << message.message << std::endl;
}
