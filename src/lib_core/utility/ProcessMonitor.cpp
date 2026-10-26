#include "ProcessMonitor.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>

#include "SerialQueue.h"
#include "logging.h"
#include "utilityString.h"

ProcessMonitor::ProcessMonitor()
{
	if (QCoreApplication::instance() == nullptr)
	{
		m_queue = execution::SerialQueue::createWithOwnPool();
		return;
	}
	m_context = new QObject();
	m_thread.setObjectName(QStringLiteral("ProcessMonitor"));
	m_context->moveToThread(&m_thread);
	m_thread.start();
}

ProcessMonitor::~ProcessMonitor()
{
	if (m_queue)
	{
		m_queue->stop();	// no `ended` any more
		utility::killRunningProcesses();
		m_waiters.clear();	// joined
		return;
	}
	// The processes are the context's children: deleting it kills what still runs.
	QMetaObject::invokeMethod(
		m_context,
		[context = m_context]() {
			for (QProcess* process: context->findChildren<QProcess*>())
			{
				QObject::disconnect(process, nullptr, nullptr, nullptr);	// no `ended` any more
			}
			delete context;
		},
		Qt::BlockingQueuedConnection);
	m_thread.quit();
	m_thread.wait();
}

void ProcessMonitor::start(
	const std::string& command,
	const std::vector<std::string>& arguments,
	std::function<void(utility::ProcessOutput)> ended)
{
	if (m_queue)
	{
		m_waiters.emplace_back([queue = m_queue, command, arguments, ended = std::move(ended)]() {
			utility::ProcessOutput output =
				utility::executeProcess(command, arguments, FilePath(), false, utility::INFINITE_TIMEOUT);
			queue->post([ended, output = std::move(output)]() { ended(output); });
		});
		return;
	}
	post([context = m_context, command, arguments, ended = std::move(ended)]() {
		const std::string resolvedCommand = utility::searchPath(command);

		QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifndef Q_OS_WIN
		// As executeProcess: Unix only, the prefix would corrupt Windows' PATH.
		env.insert(
			QStringLiteral("PATH"),
			QStringLiteral("/opt/local/bin:/usr/local/bin:") + env.value(QStringLiteral("PATH")));
#endif

		auto* process = new QProcess(context);
		process->setProcessEnvironment(env);
		process->setProcessChannelMode(QProcess::MergedChannels);

		// What it writes, read as it comes.
		auto output = std::make_shared<std::string>();
		QObject::connect(process, &QProcess::readyReadStandardOutput, process, [process, output]() {
			*output += process->readAllStandardOutput().toStdString();
		});

		// Ended: it exited, or it did not start. Once.
		auto report = [process, output, ended, command, resolvedCommand, arguments, env](bool started) {
			QObject::disconnect(process, nullptr, nullptr, nullptr);
			*output += process->readAllStandardOutput().toStdString();
			utility::ProcessOutput result;
			result.output = utility::trim(*output);
			result.exitCode = started ? process->exitCode() : -1;
			if (!started)
			{
				result.error = process->errorString().toStdString();
				LOG_ERROR_BARE("Process error: " + result.error);
			}
			result.processInfo = utility::utility_app_detail::buildProcessInfo(
				command, resolvedCommand, arguments, FilePath(), env, *process, started);
			process->deleteLater();
			ended(std::move(result));
		};
		QObject::connect(
			process,
			&QProcess::finished,
			process,
			[report](int, QProcess::ExitStatus) { report(true); });
		QObject::connect(process, &QProcess::errorOccurred, process, [report](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart)
			{
				report(false);
			}
		});

		QStringList qArguments;
		qArguments.reserve(static_cast<int>(arguments.size()));
		for (const std::string& argument: arguments)
		{
			qArguments << QString::fromStdString(argument);
		}
		process->start(QString::fromStdString(resolvedCommand), qArguments);
		process->closeWriteChannel();
	});
}

void ProcessMonitor::post(std::function<void()> work)
{
	if (m_queue)
	{
		m_queue->post(std::move(work));
		return;
	}
	QMetaObject::invokeMethod(m_context, std::move(work), Qt::QueuedConnection);
}

void ProcessMonitor::killAll()
{
	if (m_queue)
	{
		utility::killRunningProcesses();
		return;
	}
	post([context = m_context]() {
		for (QProcess* process: context->findChildren<QProcess*>())
		{
			if (process->state() != QProcess::NotRunning)
			{
				process->kill();
			}
		}
	});
}
