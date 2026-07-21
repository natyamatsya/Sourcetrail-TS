#include "QtIDECommunicationController.h"
#include "UiPost.h"

#include <functional>

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

QtIDECommunicationController::QtIDECommunicationController(QObject* parent, StorageAccess* storageAccess)
	: IDECommunicationController(storageAccess), m_tcpWrapper(parent)
{
	m_tcpWrapper.setReadCallback([this](const std::string &message) { handleIncomingMessage(message); });
}

QtIDECommunicationController::~QtIDECommunicationController() = default;

void QtIDECommunicationController::startListening()
{
	execution::qt::onUi(&m_tcpWrapper, [=, this]() {
		ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();
		m_tcpWrapper.setServerPort(appSettings->getSourcetrailPort());
		m_tcpWrapper.setClientPort(appSettings->getPluginPort());
		m_tcpWrapper.startListening();

		sendUpdatePing();
	});
}

void QtIDECommunicationController::stopListening()
{
	execution::qt::onUi(&m_tcpWrapper, [=, this]() { m_tcpWrapper.stopListening(); });
}

bool QtIDECommunicationController::isListening() const
{
	return m_tcpWrapper.isListening();
}

void QtIDECommunicationController::sendMessage(const std::string& message) const
{
	m_tcpWrapper.sendMessage(message);
}
