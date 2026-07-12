#include "QtLogBridge.h"

#include <mutex>

#include <QLoggingCategory>
#include <QString>
#include <QtGlobal>

namespace utility::qt
{
namespace
{
// Process-global: Qt allows one message handler. This assumes a single agent
// controller per process (the multi-instance design spawns separate processes, so
// that holds); a second installQtLogSink would overwrite the first's sink.
std::mutex g_mutex;
QtLogSink g_sink;
QtMessageHandler g_prev = nullptr;
bool g_installed = false;

int levelOf(QtMsgType type)
{
	switch (type)
	{
	case QtWarningMsg: return 1;
	case QtCriticalMsg:
	case QtFatalMsg: return 2;
	default: return 0;	// Debug / Info
	}
}

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
	QtMessageHandler prev = nullptr;
	QtLogSink sink;
	{
		const std::lock_guard<std::mutex> lock(g_mutex);
		prev = g_prev;
		sink = g_sink;
	}
	if (prev != nullptr)
	{
		prev(type, ctx, msg);	// chain: keep file/console logging
	}
	if (sink)
	{
		sink(levelOf(type), ctx.category, msg.toStdString(), ctx.file, ctx.function, ctx.line);
	}
}
}	 // namespace

void installQtLogSink(QtLogSink sink)
{
	const std::lock_guard<std::mutex> lock(g_mutex);
	if (sink)
	{
		g_sink = std::move(sink);
		if (!g_installed)
		{
			g_prev = qInstallMessageHandler(handler);
			g_installed = true;
		}
	}
	else
	{
		g_sink = nullptr;
		if (g_installed)
		{
			qInstallMessageHandler(g_prev);
			g_prev = nullptr;
			g_installed = false;
		}
	}
}

void setQtLogRules(const std::string& rules)
{
	QLoggingCategory::setFilterRules(QString::fromStdString(rules));
}
}	 // namespace utility::qt
