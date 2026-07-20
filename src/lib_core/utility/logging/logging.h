#ifndef LOGGING_H
#define LOGGING_H

// In a module purview only the macro definitions survive: the backend is modularized (srctrl.logging),
// so a textual LogManager declaration here would clash with the imported, module-attached one. Module
// wrappers therefore include this header AFTER `#define SRCTRL_MODULE_PURVIEW` and get LogManager (and
// std::stringstream) from `import srctrl.logging` (+ their own std handling); the macro expansions then
// name the imported entities. Classic TUs are untouched.
#ifndef SRCTRL_MODULE_PURVIEW
#include <sstream>

#include "LogManager.h"
#endif

/**
 * @brief Macros to simplify usage of the log manager.
 *
 * This header is the classic compat shim for the C++20-modules migration: the macros are kept
 * unchanged so the ~489 existing LOG_* call sites compile untouched. New/module code should prefer the
 * module-native front end -- `import srctrl.logging;` then srctrl::log::error("{}", x) / error_lazy(...)
 * -- which captures the call site with std::source_location instead of __FILE__/__FUNCTION__/__LINE__
 * and needs no macro. Both front ends route to the same LogManager backend (see LogFacade.h).
 */
#define LOG_INFO(__str__)                                                                          \
	do                                                                                             \
	{                                                                                              \
		LogManager::getInstance()->logInfo(__str__, __FILE__, __FUNCTION__, __LINE__);             \
	} while (false)

#define LOG_WARNING(__str__)                                                                       \
	do                                                                                             \
	{                                                                                              \
		LogManager::getInstance()->logWarning(__str__, __FILE__, __FUNCTION__, __LINE__);          \
	} while (false)

#define LOG_ERROR(__str__)                                                                         \
	do                                                                                             \
	{                                                                                              \
		LogManager::getInstance()->logError(__str__, __FILE__, __FUNCTION__, __LINE__);            \
	} while (false)

#define LOG_INFO_BARE(__str__)                                                                     \
	do                                                                                             \
	{                                                                                              \
		LogManager::getInstance()->logInfo(__str__, "", "", 0);                                    \
	} while (false)

#define LOG_WARNING_BARE(__str__)                                                                  \
	do                                                                                             \
	{                                                                                              \
		LogManager::getInstance()->logWarning(__str__, "", "", 0);                                 \
	} while (false)

#define LOG_ERROR_BARE(__str__)                                                                    \
	do                                                                                             \
	{                                                                                              \
		LogManager::getInstance()->logError(__str__, "", "", 0);                                   \
	} while (false)

#define LOG_INFO_STREAM(__s__)                                                                     \
	do                                                                                             \
	{                                                                                              \
		if (LogManager::getInstance()->getLoggingEnabled())                                        \
		{                                                                                          \
			std::stringstream __ss__;                                                              \
			__ss__ __s__;                                                                          \
			LogManager::getInstance()->logInfo(__ss__.str(), __FILE__, __FUNCTION__, __LINE__);    \
		}                                                                                          \
	} while (false)

#define LOG_WARNING_STREAM(__s__)                                                                  \
	do                                                                                             \
	{                                                                                              \
		if (LogManager::getInstance()->getLoggingEnabled())                                        \
		{                                                                                          \
			std::stringstream __ss__;                                                              \
			__ss__ __s__;                                                                          \
			LogManager::getInstance()->logWarning(__ss__.str(), __FILE__, __FUNCTION__, __LINE__); \
		}                                                                                          \
	} while (false)

#define LOG_ERROR_STREAM(__s__)                                                                    \
	do                                                                                             \
	{                                                                                              \
		if (LogManager::getInstance()->getLoggingEnabled())                                        \
		{                                                                                          \
			std::stringstream __ss__;                                                              \
			__ss__ __s__;                                                                          \
			LogManager::getInstance()->logError(__ss__.str(), __FILE__, __FUNCTION__, __LINE__);   \
		}                                                                                          \
	} while (false)

#define LOG_INFO_STREAM_BARE(__s__)                                                                \
	do                                                                                             \
	{                                                                                              \
		if (LogManager::getInstance()->getLoggingEnabled())                                        \
		{                                                                                          \
			std::stringstream __ss__;                                                              \
			__ss__ __s__;                                                                          \
			LogManager::getInstance()->logInfo(__ss__.str(), "", "", 0);                           \
		}                                                                                          \
	} while (false)

#define LOG_WARNING_STREAM_BARE(__s__)                                                             \
	do                                                                                             \
	{                                                                                              \
		if (LogManager::getInstance()->getLoggingEnabled())                                        \
		{                                                                                          \
			std::stringstream __ss__;                                                              \
			__ss__ __s__;                                                                          \
			LogManager::getInstance()->logWarning(__ss__.str(), "", "", 0);                        \
		}                                                                                          \
	} while (false)

#define LOG_ERROR_STREAM_BARE(__s__)                                                               \
	do                                                                                             \
	{                                                                                              \
		if (LogManager::getInstance()->getLoggingEnabled())                                        \
		{                                                                                          \
			std::stringstream __ss__;                                                              \
			__ss__ __s__;                                                                          \
			LogManager::getInstance()->logError(__ss__.str(), "", "", 0);                          \
		}                                                                                          \
	} while (false)

#endif	  // LOGGING_H
