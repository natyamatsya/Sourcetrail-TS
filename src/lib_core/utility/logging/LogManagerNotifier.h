#ifndef LOG_MANAGER_NOTIFIER_H
#define LOG_MANAGER_NOTIFIER_H

// The messaging seam of the logging backend. LogManager::setLoggingEnabled announces the toggle via
// MessageStatus (+ a version banner), but messaging is deliberately classic — inlining that body would
// drag the messaging core into every logging consumer and the srctrl.logging module. So the
// notification is an out-of-line free function: declared here (include-free, so this header is safe in
// any context, global module fragment included) and defined in LogManagerNotifier.cpp, which keeps the
// MessageStatus/Version includes to itself. Global-module entities keep their ordinary mangling, so the
// classic definition links from module code.
namespace log_manager_detail
{
// Logs the toggle banner and dispatches the MessageStatus notification. Called by
// LogManager::setLoggingEnabled AFTER the flag has been updated (the banner must pass the new state's
// own gate).
void notifyLoggingToggled(bool enabled);
}	 // namespace log_manager_detail

#endif	  // LOG_MANAGER_NOTIFIER_H
