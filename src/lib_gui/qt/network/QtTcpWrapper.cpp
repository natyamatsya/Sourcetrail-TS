// Module build: LOG_* macros stay textual; backend via `import srctrl.logging` below.
#ifdef SRCTRL_MODULE_BUILD
#define SRCTRL_LOGGING_VIA_IMPORT
#endif

#include "QtTcpWrapper.h"
#include <qdatastream.h>

#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.logging;
#endif

QtTcpWrapper::QtTcpWrapper(
	QObject* parent, const std::string& ip, const quint16 serverPort, const quint16 clientPort)
	: QObject(parent), m_serverPort(serverPort), m_clientPort(clientPort), m_ip(ip)
{
	m_tcpServer = new QTcpServer(this);

	connect(m_tcpServer, &QTcpServer::newConnection, this, &QtTcpWrapper::acceptConnection);
}

void QtTcpWrapper::startListening()
{
	if (!m_tcpServer->listen(QHostAddress::LocalHost, m_serverPort))
	{
		LOG_ERROR_STREAM(
			<< "TCP server failed to start with error: \"" + m_tcpServer->errorString().toStdString() +
				"\". Unable to listen for IDE plugin messages.");
	}
}

void QtTcpWrapper::stopListening()
{
	if (isListening())
	{
		m_tcpServer->close();
	}
}

QtTcpWrapper::~QtTcpWrapper()
{
	if (m_tcpServer != nullptr)
	{
		if (m_tcpServer->isListening())
		{
			m_tcpServer->close();
		}

		delete m_tcpServer;
	}
}

void QtTcpWrapper::sendMessage(const std::string& message) const
{
	// Without waiting for the connection: mostly no plugin listens, and a refused
	// connect to localhost takes about 2 s on Windows (it retries the SYN). Waiting
	// held up the thread that sends, the app's task queue, on every focus ping.
	// The socket lives on this wrapper's (the GUI) thread and deletes itself.
	auto* socket = new QTcpSocket();
	socket->moveToThread(thread());
	connect(socket, &QTcpSocket::connected, socket, [socket, data = QString::fromStdString(message).toUtf8()]() {
		socket->write(data);
		socket->disconnectFromHost();
	});
	connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
	connect(socket, &QTcpSocket::errorOccurred, socket, &QObject::deleteLater);
	QMetaObject::invokeMethod(
		socket, [socket, port = m_clientPort]() { socket->connectToHost(QHostAddress::LocalHost, port); });
}

void QtTcpWrapper::setReadCallback(const std::function<void(const std::string&)>& callback)
{
	m_readCallback = callback;
}

quint16 QtTcpWrapper::getServerPort() const
{
	return m_serverPort;
}

void QtTcpWrapper::setServerPort(const quint16 serverPort)
{
	m_serverPort = serverPort;
}

quint16 QtTcpWrapper::getClientPort() const
{
	return m_clientPort;
}

void QtTcpWrapper::setClientPort(const quint16 clientPort)
{
	m_clientPort = clientPort;
}

bool QtTcpWrapper::isListening() const
{
	return m_tcpServer->isListening();
}

void QtTcpWrapper::acceptConnection()
{
	m_tcpClient = m_tcpServer->nextPendingConnection();

	connect(m_tcpClient, &QTcpSocket::readyRead, this, &QtTcpWrapper::startRead);
}

void QtTcpWrapper::startRead()
{
	QByteArray byteArray;
	while (!m_tcpClient->atEnd())
	{
		QByteArray data = m_tcpClient->read(128);
		byteArray += data;
	}

	/*QDataStream stream(&byteArray, QIODevice::ReadOnly);
	QString string;
	stream >> string;*/

	QString message = QString::fromUtf8(byteArray);

	if (m_readCallback != nullptr)
	{
		m_readCallback(message.toStdString());
	}

	/*char buffer[1024] = { 0 };
	m_tcpClient->read(buffer, m_tcpClient->bytesAvailable());

	m_tcpClient->close();

	if (m_readCallback != nullptr)
	{
		m_readCallback(buffer);
	}*/
}
