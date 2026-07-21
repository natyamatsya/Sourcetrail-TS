// Module build: LOG_* macros stay textual; backend via `import srctrl.logging` below.
#ifdef SRCTRL_MODULE_BUILD
#define SRCTRL_LOGGING_VIA_IMPORT
#endif

#include "QtRequest.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <sstream>   // LOG_*_STREAM builds a std::stringstream; was transitive via logging.h

#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.logging;
#endif

QtRequest::QtRequest()
{
	m_networkManager = new QNetworkAccessManager(this);
	QObject::connect(m_networkManager, &QNetworkAccessManager::finished, this, &QtRequest::finished);
}

void QtRequest::sendRequest(const QString& url)
{
	LOG_INFO_STREAM(<< "send HTTP request: " << url.toStdString());

	try
	{
		QNetworkRequest request;
		request.setSslConfiguration(QSslConfiguration::defaultConfiguration());
		request.setUrl(QUrl(url));
		m_networkManager->get(request);
	}
	catch (...)
	{
		LOG_ERROR("Exception thrown while processing HTTP request.");
		QByteArray bytes;
		emit receivedData(bytes);
	}
}

void QtRequest::finished(QNetworkReply* reply)
{
	QVariant statusCodeV = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	QVariant redirectionTargetUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);

	Q_UNUSED(statusCodeV);
	Q_UNUSED(redirectionTargetUrl);

	if (reply->error() != QNetworkReply::NoError)
	{
		LOG_ERROR_STREAM(<< "An error occurred during http request. ERRORCODE: " << reply->error());
	}

	QByteArray bytes = reply->readAll();
	LOG_INFO_STREAM(<< "received HTTP reply: " << bytes.toStdString());

	reply->deleteLater();

	emit receivedData(bytes);
}
