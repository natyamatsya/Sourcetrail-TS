#ifndef QT_LOG_BRIDGE_H
#define QT_LOG_BRIDGE_H

#include <functional>
#include <string>

// Bridge Qt's categorized logging to the agent-control layer. Installs a
// qInstallMessageHandler that forwards every Qt message to a sink (chained to the
// previous handler, so file/console logging survives), and exposes
// QLoggingCategory::setFilterRules. Lives in lib_gui (Qt); the controller in lib
// drives it via these plain-typed entry points. See DESIGN_AGENT_UI_CONTROL.md.
namespace utility::qt
{
//! (level 0=Info/1=Warning/2=Error, category, message, file, function, line).
//! Called on whatever thread logged — the sink must be thread-safe.
using QtLogSink = std::function<
	void(int level, const char* category, const std::string& message, const char* file, const char* function, int line)>;

//! Install (non-null) or uninstall (null) the agent Qt-log sink.
void installQtLogSink(QtLogSink sink);

//! Apply Qt category filter rules ("qt.qpa.*=false\nmyapp.net.debug=true"); "" resets.
void setQtLogRules(const std::string& rules);
}	 // namespace utility::qt

#endif	  // QT_LOG_BRIDGE_H
